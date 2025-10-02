#include "App.h"

#include <array>
#include <optional>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <string_view>

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <ktx.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_glfw.h>

#include "Utils.h"

namespace im
{
	static constexpr uint32_t gMaxTextures = 1024;

	App::App()
	{
		InitWindow();

		mDevice = std::make_unique<Device>(mWindow);
		mBindlessSet = std::make_unique<BindlessSet>(*mDevice, gMaxTextures);

		InitCommandPool();
		InitDepthBuffer();
		InitPipeline();
		InitCommandBuffers();
		InitSyncPrimitives();
		InitImGui();
		InitMeshes();
		InitUniformBuffers();
		InitCubemap();
		InitShadowResources();
		InitDescriptors();
	}

	App::~App()
	{
		const auto dev = mDevice->Get();
		const auto allocator = mDevice->GetAllocator();

		mDevice->WaitIdle();

		mShadowPipe.reset();
		mShadowPipeLayout.reset();

		mEnvMapPipe.reset();
		mEnvMapPipeLayout.reset();
		mEnvMapSetLayout.reset();

		vkDestroySampler(dev, mShadowMapSampler, nullptr);
		mShadowMap.reset();

		vkDestroySampler(dev, mEnvMapSampler, nullptr);
		mEnvMap.reset();

		vkDestroySampler(dev, mTextureSampler, nullptr);
		mTexture.reset();
		mNormalMap.reset();

		mMeshes.clear();
		mUniformBuffers.clear();

		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		for (const auto& fence : mRenderFences)
			vkDestroyFence(dev, fence, nullptr);
		
		for (const auto& sem : mRenderSemaphores)
			vkDestroySemaphore(dev, sem, nullptr);

		for (const auto& sem : mAcquireSemaphores)
			vkDestroySemaphore(dev, sem, nullptr);

		vkDestroyDescriptorPool(dev, mGlobalPool, nullptr);

		mPipe.reset();
		mPipeLayout.reset();
		mGlobalLayout.reset();

		CleanupSwapchain();

		vkDestroyCommandPool(dev, mTransientPool, nullptr);
		vkDestroyCommandPool(dev, mCommandPool, nullptr);

		mBindlessSet.reset();
		mDevice.reset();

		glfwTerminate();
	}

	void App::Run()
	{
		float lastTime = glfwGetTime();

		while (!glfwWindowShouldClose(mWindow))
		{
			glfwPollEvents();
			const float currentTime = glfwGetTime();
			const float deltaTime = currentTime - lastTime;

			Update(deltaTime);
			Render();

			lastTime = currentTime;
		}
	}

	void App::Update(float deltaTime)
	{
		if (glfwGetKey(mWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(mWindow, GLFW_TRUE);

		const float moveFactor = 2.5f;

		const auto front = mCamera.front;
		glm::vec3 up(0.0f, 1.0f, 0.0f);
		glm::vec3 right = glm::normalize(glm::cross(front, up));

		if (glfwGetKey(mWindow, GLFW_KEY_W) == GLFW_PRESS)
		{
			mCamera.position += front * deltaTime * moveFactor;
		}
		else if (glfwGetKey(mWindow, GLFW_KEY_S) == GLFW_PRESS)
		{
			mCamera.position += -front * deltaTime * moveFactor;
		}

		if (glfwGetKey(mWindow, GLFW_KEY_A) == GLFW_PRESS)
		{
			mCamera.position += -right * deltaTime * moveFactor;
		}
		else if (glfwGetKey(mWindow, GLFW_KEY_D) == GLFW_PRESS)
		{
			mCamera.position += right * deltaTime * moveFactor;
		}

		mCamera.Update();
	}

	void App::Render()
	{
		Swapchain& swapchain = mDevice->GetSwapchain();
		const auto dev = mDevice->Get();
		const auto swapExtent = swapchain.GetExtent();

		VK_CHECK(vkWaitForFences(dev, 1, &mRenderFences[mFrameIndex], VK_TRUE, UINT64_MAX));

		auto [res, imageIndex] = swapchain.AcquireNextImage(mAcquireSemaphores[mSemaphoreIndex]);
		if (res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RecreateSwapchain();
			return;
		}
		else
		{
			VK_CHECK(res);
		}

		VK_CHECK(vkResetFences(dev, 1, &mRenderFences[mFrameIndex]));

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		
		VK_CHECK(vkBeginCommandBuffer(mCommandBuffers[mFrameIndex], &beginInfo));

		glm::vec3 lightDir = glm::vec3(2.0f * sin(glfwGetTime()), 2.0f, -2.0f * cos(glfwGetTime()));

		glm::mat4 lightView = glm::lookAt(
			lightDir,
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));

		// Transform and project to "light space"
		constexpr float shadowProjDim = 10.0f;
		glm::mat4 lightProj = glm::ortho(-shadowProjDim, shadowProjDim, -shadowProjDim, shadowProjDim, 0.1f, shadowProjDim); // No perspective skew for dir light

		DrawShadowMap(mCommandBuffers[mFrameIndex], lightView, lightProj);

		TransitionSwapchainImage(
			swapchain.GetImages()[imageIndex],
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ACCESS_2_NONE,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

		auto [viewport, scissor] = utils::ViewportAndScissor(swapExtent);
		vkCmdSetViewport(mCommandBuffers[mFrameIndex], 0, 1, &viewport);
		vkCmdSetScissor(mCommandBuffers[mFrameIndex], 0, 1, &scissor);

		VkClearValue color{};
		color.color = { 0.0f, 0.0f, 0.0f, 1.0f };

		VkClearValue depth{};
		depth.depthStencil.depth = 1.0f;
		depth.depthStencil.stencil = 0;

		VkRenderingAttachmentInfo colorAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttach.clearValue = color;
		colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttach.imageView = swapchain.GetViews()[imageIndex];
		colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		
		VkRenderingAttachmentInfo depthAttach{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthAttach.clearValue = depth;
		depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttach.imageView = mDepthImage->GetView();
		depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttach;
		renderingInfo.pDepthAttachment = &depthAttach;
		renderingInfo.renderArea = scissor;
		renderingInfo.layerCount = 1;

		vkCmdBeginRendering(mCommandBuffers[mFrameIndex], &renderingInfo);

		CubemapData cubemapData{};
		glm::mat4 view = mCamera.GetViewMatrix();
		glm::mat4 proj = glm::perspective(glm::radians(75.0f), static_cast<float>(swapExtent.width) / swapExtent.height, 0.1f, 100.0f);
		cubemapData.vpInverse = glm::inverse(proj * glm::mat4(glm::mat3(view))); // Remove translations

		// Cubemap pass
		vkCmdBindPipeline(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mEnvMapPipe->Get());
		vkCmdPushConstants(mCommandBuffers[mFrameIndex], mEnvMapPipeLayout->Get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(cubemapData), &cubemapData);
		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mEnvMapPipeLayout->Get(), 0, 1, &mEnvMapSet, 0, nullptr);
		vkCmdDraw(mCommandBuffers[mFrameIndex], 3, 1, 0, 0);

		// Forward pass
		GlobalPassData passData{};
		passData.view = view;
		passData.viewProj = proj * view;
		passData.viewProjLight = lightProj * lightView;
		passData.viewInverse = glm::inverse(view);
		passData.lightDir = view * glm::vec4(lightDir, 0.0f);

		void* globalBufferData = mUniformBuffers[mFrameIndex]->Map();
		std::memcpy(globalBufferData, &passData, sizeof(passData));
		mUniformBuffers[mFrameIndex]->Unmap();

		VkDescriptorSet descSets[] = { mGlobalSets[mFrameIndex], mBindlessSet->Get() };
		vkCmdBindPipeline(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipe->Get());
		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout->Get(), 0, 2, descSets, 0, nullptr);

		ObjectData pushConsts{};
		for (const auto& mesh : mMeshes)
		{
			pushConsts.model = mesh.transform;
			pushConsts.textureHandle = mesh.textureHandle;
			pushConsts.normalMapHandle = mesh.normalMapHandle;
			vkCmdPushConstants(mCommandBuffers[mFrameIndex],
				mPipeLayout->Get(),
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConsts), & pushConsts);

			VkDeviceSize offsets[] = { 0 };
			VkBuffer vertexBuffer = mesh.vertexBuffer->Get();
			vkCmdBindVertexBuffers(mCommandBuffers[mFrameIndex], 0, 1, &vertexBuffer, offsets);
			vkCmdBindIndexBuffer(mCommandBuffers[mFrameIndex], mesh.indexBuffer->Get(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(mCommandBuffers[mFrameIndex], mesh.indexCount, 1, 0, 0, 0);
		}

		ImGui::ShowDemoWindow();

		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), mCommandBuffers[mFrameIndex]);

		vkCmdEndRendering(mCommandBuffers[mFrameIndex]);

		TransitionSwapchainImage(
			swapchain.GetImages()[imageIndex],
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

		VK_CHECK(vkEndCommandBuffer(mCommandBuffers[mFrameIndex]));

		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &mCommandBuffers[mFrameIndex];
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &mAcquireSemaphores[mSemaphoreIndex];
		submitInfo.pWaitDstStageMask = &waitStage;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &mRenderSemaphores[imageIndex];

		VK_CHECK(vkQueueSubmit(mDevice->GetGraphicsQueue(), 1, &submitInfo, mRenderFences[mFrameIndex]));

		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();

		res = swapchain.Present(mRenderSemaphores[imageIndex]);
		if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || mFramebufferResized)
		{
			mFramebufferResized = false;
			RecreateSwapchain();
		}
		else
		{
			VK_CHECK(res);
		}

		mSemaphoreIndex = (mSemaphoreIndex + 1) % mAcquireSemaphores.size();
		mFrameIndex = (mFrameIndex + 1) % MaxFramesInFlight;
	}

	void App::DrawShadowMap(VkCommandBuffer commandBuffer, const glm::mat4& lightView, const glm::mat4& lightProj)
	{
		// First pass: shadow map generation
		mShadowMap->Barrier(commandBuffer,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, // WAR hazard - only need execution dep
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

		const VkExtent2D shadowMapExtent = mShadowMap->GetExtent();
		const VkRect2D shadowRenderArea = { { 0, 0 }, shadowMapExtent };

		const auto shadowAttachment = utils::RenderingDepthAttachment(
			mShadowMap->GetView(),
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE);

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.pDepthAttachment = &shadowAttachment;
		renderingInfo.renderArea = shadowRenderArea;
		renderingInfo.layerCount = 1;

		vkCmdBeginRendering(commandBuffer, &renderingInfo);

		const auto [viewport, scissor] = utils::ViewportAndScissor(shadowMapExtent);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		{
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipe->Get());

			ShadowPassData pushConsts{};
			glm::mat4 viewProj = lightProj * lightView;
			for (const auto& mesh : mMeshes)
			{
				pushConsts.mvp = viewProj * mesh.transform;
				vkCmdPushConstants(commandBuffer, mShadowPipeLayout->Get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);

				VkDeviceSize offsets[] = { 0 };
				VkBuffer vertexBuffer = mesh.vertexBuffer->Get();
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
				vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer->Get(), 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
			}
		}

		vkCmdEndRendering(commandBuffer);

		mShadowMap->Barrier(mCommandBuffers[mFrameIndex],
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT);
	}

	void App::InitWindow()
	{
		constexpr uint32_t defaultWindowWidth = 1280;
		constexpr uint32_t defaultWindowHeight = 720;

		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		mWindow = glfwCreateWindow(defaultWindowWidth, defaultWindowHeight, "VulkanApp", nullptr, nullptr);
		if (!mWindow)
		{
			std::cerr << "Failed to create window!\n";
			return;
		}

		glfwSetWindowUserPointer(mWindow, this);
		glfwSetFramebufferSizeCallback(mWindow, FramebufferSizeCallback);
		glfwSetCursorPosCallback(mWindow, MousePositionCallback);
		glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}

	void App::InitCommandPool()
	{
		const auto dev = mDevice->Get();
		VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.queueFamilyIndex = mDevice->GetGraphicsIndex();
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		VK_CHECK(vkCreateCommandPool(dev, &poolInfo, nullptr, &mCommandPool));

		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		VK_CHECK(vkCreateCommandPool(dev, &poolInfo, nullptr, &mTransientPool));
	}

	void App::InitDepthBuffer()
	{
		const auto swapExtent = mDevice->GetSwapchain().GetExtent();
		const VkFormat format = mDevice->GetSupportedFormat
		(
			{
				VK_FORMAT_D32_SFLOAT,
				VK_FORMAT_D32_SFLOAT_S8_UINT,
				VK_FORMAT_D24_UNORM_S8_UINT
			},
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);

		mDepthImage = std::make_unique<Texture2D>(*mDevice, format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, swapExtent.width, swapExtent.height, false);

		auto cmds = CreateImmediateCommandBuffer();
		mDepthImage->Barrier(cmds,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
		SubmitImmediateCommandBuffer(cmds);
	}

	void App::InitPipeline()
	{
		const auto dev = mDevice->Get();
		const auto shaderSource = utils::ReadFile("Assets/Shaders/Basic.spv");
		VkShaderModule shader = CreateShader(shaderSource);

		VkPushConstantRange pcRange{};
		pcRange.offset = 0;
		pcRange.size = sizeof(ObjectData);
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		mGlobalLayout = std::make_unique<DescriptorSetLayout>(*mDevice);
		mGlobalLayout->
			AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)	// Lighting
			.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)						// Cubemap
			.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)						// Shadow Map
			.Commit();

		const std::vector<VkDescriptorSetLayout> setLayouts{ mGlobalLayout->Get(), mBindlessSet->GetSetLayout() };
		const std::vector<VkPushConstantRange> pcRanges{ pcRange };
		mPipeLayout = std::make_unique<PipelineLayout>(*mDevice, setLayouts, pcRanges);
		
		VkVertexInputBindingDescription inputBinding{};
		inputBinding.binding = 0;
		inputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		inputBinding.stride = sizeof(Vertex);

		const auto attribs = Vertex::GetInputAttributes();
		mPipe = std::make_unique<GraphicsPipeline>(*mDevice, *mPipeLayout);
		mPipe->AddShader(shader, VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.AddShader(shader, VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain")
			.SetVertexInput({ inputBinding }, std::vector<VkVertexInputAttributeDescription>(attribs.begin(), attribs.end()))
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.AddColorAttachment(mDevice->GetSwapchain().GetFormat())
			.SetDepthAttachment(mDepthImage->GetFormat(), true)
			.Commit();

		vkDestroyShaderModule(dev, shader, nullptr);
	}

	void App::InitCommandBuffers()
	{
		mCommandBuffers.clear();
		mCommandBuffers.resize(MaxFramesInFlight);

		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandBufferCount = MaxFramesInFlight;
		allocInfo.commandPool = mCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		
		VK_CHECK(vkAllocateCommandBuffers(mDevice->Get(), &allocInfo, mCommandBuffers.data()));
	}

	void App::InitSyncPrimitives()
	{
		const auto dev = mDevice->Get();
		mAcquireSemaphores.clear();
		mRenderSemaphores.clear();
		mRenderFences.clear();

		mAcquireSemaphores.resize(MaxFramesInFlight);
		mRenderSemaphores.resize(MaxFramesInFlight);
		mRenderFences.resize(MaxFramesInFlight);

		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < mDevice->GetSwapchain().GetViews().size(); ++i)
		{
			VK_CHECK(vkCreateSemaphore(dev, &semaphoreInfo, nullptr, &mAcquireSemaphores[i]));
			VK_CHECK(vkCreateSemaphore(dev, &semaphoreInfo, nullptr, &mRenderSemaphores[i]));
		}

		for (size_t i = 0; i < MaxFramesInFlight; ++i)
		{
			VK_CHECK(vkCreateFence(dev, &fenceInfo, nullptr, &mRenderFences[i]));
		}
	}

	void App::InitImGui()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		ImGui_ImplGlfw_InitForVulkan(mWindow, true);

		const auto format = mDevice->GetSwapchain().GetFormat();

		VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &format;
		renderingInfo.depthAttachmentFormat = mDepthImage->GetFormat();

		ImGui_ImplVulkan_InitInfo imguiVulkanInfo{};
		imguiVulkanInfo.ApiVersion = VK_API_VERSION_1_4;
		imguiVulkanInfo.CheckVkResultFn = [](VkResult err) { VK_CHECK(err); };
		imguiVulkanInfo.DescriptorPoolSize = 128;
		imguiVulkanInfo.Device = mDevice->Get();
		imguiVulkanInfo.ImageCount = mDevice->GetSwapchain().GetViews().size();
		imguiVulkanInfo.MinImageCount = MaxFramesInFlight;
		imguiVulkanInfo.Instance = mDevice->GetInstance();
		imguiVulkanInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		imguiVulkanInfo.PhysicalDevice = mDevice->GetGpu();
		imguiVulkanInfo.UseDynamicRendering = true;
		imguiVulkanInfo.PipelineRenderingCreateInfo = renderingInfo;
		imguiVulkanInfo.Queue = mDevice->GetGraphicsQueue();
		imguiVulkanInfo.QueueFamily = mDevice->GetGraphicsIndex();

		ImGui_ImplVulkan_Init(&imguiVulkanInfo);
	}

	void App::InitMeshes()
	{
		// Load texture image
		mTexture = CreateAndStageTexture("Assets/Textures/brickwall.jpg", VK_FORMAT_R8G8B8A8_SRGB, false);
		mNormalMap = CreateAndStageTexture("Assets/Textures/brickwall_normal.jpg", VK_FORMAT_R8G8B8A8_UNORM, false);

		const auto dev = mDevice->Get();
		VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		VK_CHECK(vkCreateSampler(dev, &samplerInfo, nullptr, &mTextureSampler));

		uint32_t texId = mBindlessSet->RegisterTexture(*mTexture, mTextureSampler);
		uint32_t nmapId = mBindlessSet->RegisterTexture(*mNormalMap, mTextureSampler);

		// Duck
		{
			Mesh duck;

			auto [duckVertices, duckIndices] = utils::LoadModel("Assets/Models/Duck.gltf");

			Buffer stagingDuckVerts(*mDevice, duckVertices.size() * sizeof(duckVertices[0]), duckVertices.data());
			Buffer stagingDuckIdxs(*mDevice, duckIndices.size() * sizeof(duckIndices[0]), duckIndices.data());
			duck.vertexBuffer = std::make_unique<Buffer>(
				*mDevice, stagingDuckVerts.GetSize(),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
			duck.indexBuffer = std::make_unique<Buffer>(
				*mDevice, stagingDuckIdxs.GetSize(),
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
			duck.textureHandle = texId;
			duck.normalMapHandle = nmapId;
			duck.indexCount = duckIndices.size();

			auto cmds = CreateImmediateCommandBuffer();
			CopyBuffer(cmds, stagingDuckVerts.Get(), duck.vertexBuffer->Get(), stagingDuckVerts.GetSize());
			CopyBuffer(cmds, stagingDuckIdxs.Get(), duck.indexBuffer->Get(), stagingDuckIdxs.GetSize());
			SubmitImmediateCommandBuffer(cmds);

			duck.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
			duck.transform = glm::scale(duck.transform, glm::vec3(0.01f));

			mMeshes.emplace_back(std::move(duck));
		}

		{
			Mesh plane;
			const std::vector<Vertex> planeVertices
			{
				{ { -0.5f, 0.0f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
				{ { 0.5f, 0.0f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
				{ { 0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
				{ { -0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			};

			const std::vector<uint32_t> planeIndices
			{
				0, 1, 2,
				2, 3, 0
			};

			plane.indexCount = planeIndices.size();
			const VkDeviceSize planeVertSize = planeVertices.size() * sizeof(planeVertices[0]);
			const VkDeviceSize planeIdxSize = planeIndices.size() * sizeof(planeIndices[0]);

			Buffer stagingPlaneVerts(*mDevice, planeVertSize, planeVertices.data());
			Buffer stagingPlaneIdxs(*mDevice, planeIdxSize, planeIndices.data());
			plane.vertexBuffer = std::make_unique<Buffer>(*mDevice, planeVertSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0);
			plane.indexBuffer = std::make_unique<Buffer>(*mDevice, planeIdxSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0);
			auto cmds = CreateImmediateCommandBuffer();
			CopyBuffer(cmds, stagingPlaneVerts.Get(), plane.vertexBuffer->Get(), planeVertSize);
			CopyBuffer(cmds, stagingPlaneIdxs.Get(), plane.indexBuffer->Get(), planeIdxSize);
			SubmitImmediateCommandBuffer(cmds);

			plane.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
			plane.transform = glm::scale(plane.transform, glm::vec3(5.0f));
			plane.textureHandle = texId;
			plane.normalMapHandle = nmapId;

			mMeshes.emplace_back(std::move(plane));
		}
	}

	void App::InitUniformBuffers()
	{
		const auto size = sizeof(GlobalPassData);
		mUniformBuffers.reserve(MaxFramesInFlight);
		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			mUniformBuffers.emplace_back(
				std::make_unique<Buffer>(*mDevice, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT));
		}
	}

	void App::InitCubemap()
	{
		stbi_set_flip_vertically_on_load(false); // Reversing UV coords using vp^-1, so images will be loaded in correct orientation

		const auto dev = mDevice->Get();
		const auto allocator = mDevice->GetAllocator();
		constexpr VkDeviceSize bytesPerPixel = 4;
		constexpr size_t cubemapFaces = 6;
		int width, height, channels;

		const std::array<std::filesystem::path, cubemapFaces> skyboxPaths
		{
			"Assets/Textures/Skybox/right.jpg",
			"Assets/Textures/Skybox/left.jpg",
			"Assets/Textures/Skybox/top.jpg",
			"Assets/Textures/Skybox/bottom.jpg",
			"Assets/Textures/Skybox/front.jpg",
			"Assets/Textures/Skybox/back.jpg",
		};

		std::array<stbi_uc*, cubemapFaces> cubemapData;
		for (size_t i = 0; i < cubemapFaces; ++i)
		{
			stbi_uc* data = stbi_load(skyboxPaths[i].string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
			if (!data)
			{
				std::cerr << "Failed to load cubemap!\n";
			}

			cubemapData[i] = data;
		}

		const VkDeviceSize faceSize = width * height * bytesPerPixel;
		const VkDeviceSize size = faceSize * 6;

		Buffer staging(*mDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		stbi_uc* mappedData = reinterpret_cast<stbi_uc*>(staging.Map());
		for (size_t i = 0; i < cubemapFaces; ++i)
			std::memcpy(mappedData + faceSize * i, cubemapData[i], faceSize);

		staging.Unmap();

		for (const auto& data : cubemapData)
			stbi_image_free(data);

		mEnvMap = std::make_unique<TextureCube>(*mDevice, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, width, height);

		auto cmds = CreateImmediateCommandBuffer();

		mEnvMap->Barrier(cmds,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

		VkBufferImageCopy buffer2Image = mEnvMap->CopyFromBuffer();
		vkCmdCopyBufferToImage(cmds, staging.Get(), mEnvMap->Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer2Image);

		mEnvMap->Barrier(cmds,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT_KHR);

		SubmitImmediateCommandBuffer(cmds);

		VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		VK_CHECK(vkCreateSampler(dev, &samplerInfo, nullptr, &mEnvMapSampler));
		
		// Create environment pipeline
		auto shaderSource = utils::ReadFile("Assets/Shaders/Cubemap.spv");
		VkShaderModule shader = CreateShader(shaderSource);

		mEnvMapSetLayout = std::make_unique<DescriptorSetLayout>(*mDevice);
		mEnvMapSetLayout->AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
						.Commit();

		VkPushConstantRange pcRange{};
		pcRange.offset = 0;
		pcRange.size = sizeof(CubemapData);
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		const std::vector<VkDescriptorSetLayout> setLayouts{ mEnvMapSetLayout->Get() };
		const std::vector<VkPushConstantRange> pcRanges{ pcRange };
		mEnvMapPipeLayout = std::make_unique<PipelineLayout>(*mDevice, setLayouts, pcRanges);

		mEnvMapPipe = std::make_unique<GraphicsPipeline>(*mDevice, *mEnvMapPipeLayout);
		mEnvMapPipe->AddShader(shader, VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.AddShader(shader, VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain")
			.SetVertexInput({}, {})
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.AddColorAttachment(mDevice->GetSwapchain().GetFormat())
			.SetDepthAttachment(mDepthImage->GetFormat(), false)
			.Commit();

		vkDestroyShaderModule(dev, shader, nullptr);
	}

	void App::InitShadowResources()
	{
		stbi_set_flip_vertically_on_load(false); // Reversing UV coords using vp^-1, so images will be loaded in correct orientation

		const auto dev = mDevice->Get();
		const auto allocator = mDevice->GetAllocator();

		constexpr VkDeviceSize bytesPerPixel = 4;
		constexpr size_t cubemapFaces = 6;
		constexpr uint32_t shadowWidth = 2048;
		constexpr uint32_t shadowHeight = 2048;

		const VkDeviceSize faceSize = shadowWidth * shadowHeight * bytesPerPixel;
		const VkDeviceSize size = faceSize * 6;

		mShadowMap = std::make_unique<Texture2D>(*mDevice, mDepthImage->GetFormat(),
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowWidth, shadowHeight, false);

		VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.compareEnable = VK_TRUE;
		samplerInfo.compareOp = VK_COMPARE_OP_LESS;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		VK_CHECK(vkCreateSampler(dev, &samplerInfo, nullptr, &mShadowMapSampler));

		const auto shaderSource = utils::ReadFile("Assets/Shaders/ShadowDepthPass.spv");
		VkShaderModule shader = CreateShader(shaderSource);

		VkPushConstantRange matrixRange{};
		matrixRange.offset = 0;
		matrixRange.size = sizeof(ShadowPassData);
		matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		const std::vector<VkDescriptorSetLayout> setLayouts{};
		const std::vector<VkPushConstantRange> pcRanges{ matrixRange };
		mShadowPipeLayout = std::make_unique<PipelineLayout>(*mDevice, setLayouts, pcRanges);

		VkVertexInputBindingDescription inputBinding{};
		inputBinding.binding = 0;
		inputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		inputBinding.stride = sizeof(Vertex);

		mShadowPipe = std::make_unique<GraphicsPipeline>(*mDevice, *mShadowPipeLayout);
		mShadowPipe->AddShader(shader, VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.SetVertexInput({ inputBinding }, { utils::InputAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0) })
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.SetDepthAttachment(mShadowMap->GetFormat(), true)
			.Commit();

		vkDestroyShaderModule(dev, shader, nullptr);
	}

	void App::InitDescriptors()
	{
		const auto dev = mDevice->Get();

		VkDescriptorPoolSize globalSizes[]{
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MaxFramesInFlight }, // UBOs
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } }; // Cubemap pass

		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = MaxFramesInFlight + 1;
		poolInfo.poolSizeCount = std::size(globalSizes);
		poolInfo.pPoolSizes = globalSizes;
		VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &mGlobalPool));

		mGlobalSets.resize(MaxFramesInFlight);
		VkDescriptorSetLayout globalLayouts[]{ mGlobalLayout->Get(), mGlobalLayout->Get() };
		VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocInfo.descriptorPool = mGlobalPool;
		allocInfo.descriptorSetCount = std::size(globalLayouts);
		allocInfo.pSetLayouts = globalLayouts;
		VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, mGlobalSets.data()));

		VkDescriptorSetLayout cubemapLayouts[]{ mEnvMapSetLayout->Get()};
		allocInfo.descriptorPool = mGlobalPool;
		allocInfo.descriptorSetCount = std::size(cubemapLayouts);
		allocInfo.pSetLayouts = cubemapLayouts;
		VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &mEnvMapSet));

		VkDescriptorImageInfo imageWrite{};
		imageWrite.imageView = mEnvMap->GetView();
		imageWrite.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageWrite.sampler = mEnvMapSampler;

		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.dstArrayElement = 0;
		write.dstBinding = 0;
		write.dstSet = mEnvMapSet;
		write.pImageInfo = &imageWrite;
		vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			VkDescriptorBufferInfo buffer{};
			buffer.buffer = mUniformBuffers[i]->Get();
			buffer.offset = 0;
			buffer.range = sizeof(GlobalPassData);

			VkWriteDescriptorSet bufferWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			bufferWrite.descriptorCount = 1;
			bufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bufferWrite.dstArrayElement = 0;
			bufferWrite.dstBinding = 0;
			bufferWrite.dstSet = mGlobalSets[i];
			bufferWrite.pBufferInfo = &buffer;

			// Cubemap
			VkDescriptorImageInfo cubemapInfo{};
			cubemapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			cubemapInfo.imageView = mEnvMap->GetView();
			cubemapInfo.sampler = mEnvMapSampler;

			VkWriteDescriptorSet cubemapWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			cubemapWrite.descriptorCount = 1;
			cubemapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			cubemapWrite.dstArrayElement = 0;
			cubemapWrite.dstBinding = 1;
			cubemapWrite.dstSet = mGlobalSets[i];
			cubemapWrite.pImageInfo = &cubemapInfo;

			// Shadow Map
			VkDescriptorImageInfo depthMap{};
			depthMap.imageView = mShadowMap->GetView();
			depthMap.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
			depthMap.sampler = mShadowMapSampler;

			VkWriteDescriptorSet depthMapWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			depthMapWrite.descriptorCount = 1;
			depthMapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			depthMapWrite.dstArrayElement = 0;
			depthMapWrite.dstBinding = 2;
			depthMapWrite.dstSet = mGlobalSets[i];
			depthMapWrite.pImageInfo = &depthMap;

			VkWriteDescriptorSet writes[]{ bufferWrite, cubemapWrite, depthMapWrite };
			vkUpdateDescriptorSets(dev, 3, writes, 0, nullptr);
		}
	}

	void App::CleanupSwapchain()
	{
		mDepthImage.reset();
	}

	void App::RecreateSwapchain()
	{
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(mWindow, &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(mWindow, &width, &height);
			glfwWaitEvents();
		}

		mDevice->WaitIdle();

		CleanupSwapchain();

		mDevice->GetSwapchain().Recreate();
		InitDepthBuffer();
	}

	VkShaderModule App::CreateShader(const std::vector<char>& source)
	{
		VkShaderModuleCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		shaderInfo.codeSize = source.size() * sizeof(source[0]);
		shaderInfo.pCode = reinterpret_cast<const uint32_t*>(source.data());

		VkShaderModule res;
		VK_CHECK(vkCreateShaderModule(mDevice->Get(), &shaderInfo, nullptr, &res));
		return res;
	}

	void App::TransitionSwapchainImage(
		VkImage image,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		VkAccessFlags2 srcAccess,
		VkAccessFlags2 dstAccess,
		VkPipelineStageFlags2 srcStage,
		VkPipelineStageFlags2 dstStage)
	{
		VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = srcStage;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask = dstStage;
		barrier.dstAccessMask = dstAccess;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(mCommandBuffers[mFrameIndex], &depInfo);
	}

	VkCommandBuffer App::CreateImmediateCommandBuffer()
	{
		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = mTransientPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		VkCommandBuffer cmds;
		VK_CHECK(vkAllocateCommandBuffers(mDevice->Get(), &allocInfo, &cmds));

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VK_CHECK(vkBeginCommandBuffer(cmds, &beginInfo));

		return cmds;
	}

	void App::CopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
	{
		VkBufferCopy copyRegion{};
		copyRegion.size = size;

		vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
	}

	void App::SubmitImmediateCommandBuffer(VkCommandBuffer commandBuffer)
	{
		VK_CHECK(vkEndCommandBuffer(commandBuffer));

		VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		VK_CHECK(vkQueueSubmit(mDevice->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));
		VK_CHECK(vkQueueWaitIdle(mDevice->GetGraphicsQueue()));

		vkFreeCommandBuffers(mDevice->Get(), mTransientPool, 1, &commandBuffer);
	}

	std::unique_ptr<Texture2D> App::CreateAndStageTexture(const std::filesystem::path& path, VkFormat format, bool generateMipmaps)
	{
		/*ktxTexture* texture;
		KTX_error_code res = ktxTexture_CreateFromNamedFile(path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
		if (res != KTX_SUCCESS)
		{
			std::cerr << "Failed to load KTX texture!\n";
			return nullptr;
		}

		uint32_t width = texture->baseWidth;
		uint32_t height = texture->baseHeight;
		ktx_size_t size = ktxTexture_GetImageSize(texture, 0);
		ktx_uint8_t* data = ktxTexture_GetData(texture);

		Buffer stagingTex(*mDevice, size, data);

		ktxTexture_Destroy(texture);*/

		stbi_set_flip_vertically_on_load(true);

		int width, height, channels;
		stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
		if (!data)
		{
			std::cerr << "Failed to load image from " << path << "!\n";
			return nullptr;
		}

		VkDeviceSize size = width * height * 4;
		Buffer stagingTex(*mDevice, size, data);

		auto resTex = std::make_unique<Texture2D>(*mDevice,
			format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			width, height, generateMipmaps);

		auto cmds = CreateImmediateCommandBuffer();
		resTex->Barrier(cmds,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

		VkBufferImageCopy buffer2Image = resTex->CopyFromBuffer();
		vkCmdCopyBufferToImage(cmds, stagingTex.Get(), resTex->Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer2Image);

		if (generateMipmaps)
		{
			resTex->GenerateMipmaps(cmds, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
		}
		else
		{
			resTex->Barrier(cmds,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
		}

		SubmitImmediateCommandBuffer(cmds);
		return resTex;
	}

	void App::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
	{
		App* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
		app->mFramebufferResized = true;
	}

	void App::MousePositionCallback(GLFWwindow* window, double xpos, double ypos)
	{
		static bool firstTouch = true;
		static double lastX;
		static double lastY;
		App* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
		if (firstTouch)
		{
			firstTouch = false;
			lastX = xpos;
			lastY = ypos;
		}

		constexpr float sensitivity = 0.2f;
		float deltaX = xpos - lastX;
		float deltaY = lastY - ypos;

		app->mCamera.yaw += sensitivity * deltaX;
		app->mCamera.pitch += sensitivity * deltaY;
		glfwSetCursorPos(window, lastX, lastY);
	}
}