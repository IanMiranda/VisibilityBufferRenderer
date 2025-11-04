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
	static constexpr uint32_t gMaxTextures = 512;

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
		mDevice->WaitIdle();

		mShadowPass.reset();

		mEnvMapPipe.reset();
		mEnvMapPipeLayout.reset();
		mEnvMapSetLayout.reset();

		mEnvMap.reset();
		mDiffuseMap.reset();
		mNormalMap.reset();

		mMeshes.clear();
		mUniformBuffers.clear();

		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		for (const auto& fence : mRenderFences)
			vkDestroyFence(mDevice->Get(), fence, nullptr);
		
		for (const auto& sem : mRenderSemaphores)
			vkDestroySemaphore(mDevice->Get(), sem, nullptr);

		for (const auto& sem : mAcquireSemaphores)
			vkDestroySemaphore(mDevice->Get(), sem, nullptr);

		vkDestroyDescriptorPool(mDevice->Get(), mGlobalPool, nullptr);

		mPipe.reset();
		mPipeLayout.reset();
		mGlobalLayout.reset();

		CleanupSwapchain();

		mCommandBuffers.clear();
		mImmediatePool.reset();
		mCommandPool.reset();

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
		const glm::vec3 up(0.0f, 1.0f, 0.0f);
		const glm::vec3 right = glm::normalize(glm::cross(front, up));

		if (glfwGetKey(mWindow, GLFW_KEY_W) == GLFW_PRESS)
			mCamera.position += front * deltaTime * moveFactor;
		else if (glfwGetKey(mWindow, GLFW_KEY_S) == GLFW_PRESS)
			mCamera.position += -front * deltaTime * moveFactor;

		if (glfwGetKey(mWindow, GLFW_KEY_A) == GLFW_PRESS)
			mCamera.position += -right * deltaTime * moveFactor;
		else if (glfwGetKey(mWindow, GLFW_KEY_D) == GLFW_PRESS)
			mCamera.position += right * deltaTime * moveFactor;

		mCamera.Update();
	}

	void App::Render()
	{
		Swapchain& swapchain = mDevice->GetSwapchain();

		VK_CHECK(vkWaitForFences(mDevice->Get(), 1, &mRenderFences[mFrameIndex], VK_TRUE, UINT64_MAX));

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

		VK_CHECK(vkResetFences(mDevice->Get(), 1, &mRenderFences[mFrameIndex]));

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		mCommandBuffers[mFrameIndex]->Begin();

		DrawScene(*mCommandBuffers[mFrameIndex]);
		
		mCommandBuffers[mFrameIndex]->End();

		mDevice->Submit(*(mCommandBuffers[mFrameIndex]),
			mAcquireSemaphores[mSemaphoreIndex], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			mRenderSemaphores[imageIndex], mRenderFences[mFrameIndex]);

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

	void App::DrawScene(CommandBuffer& commandBuffer)
	{
		const Swapchain& swapchain = mDevice->GetSwapchain();
		const auto swapExtent = swapchain.GetExtent();

		const glm::vec3 lightDir = glm::vec3(2.0f * sin(glfwGetTime()), 2.0f, -2.0f * cos(glfwGetTime()));
		const glm::mat4 lightView = glm::lookAt(
			lightDir,
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));

		// Transform and project to "light space"
		constexpr float shadowProjDim = 10.0f;
		const glm::mat4 lightProj = glm::ortho(-shadowProjDim, shadowProjDim, -shadowProjDim, shadowProjDim, 0.1f, shadowProjDim); // No perspective skew for dir light

		DrawShadowMap(commandBuffer, lightView, lightProj);

		TransitionSwapchainImage(
			swapchain.GetImages()[swapchain.GetImageIndex()],
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ACCESS_2_NONE,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

		commandBuffer.SetViewportAndScissor(swapExtent);

		commandBuffer.BeginRendering(
			{ utils::ColorAttachment(swapchain.GetViews()[swapchain.GetImageIndex()], VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE) },
			utils::DepthAttachment(mDepthImage->GetView(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE),
			utils::Scissor(swapExtent)
		);

		CubemapData cubemapData{};
		glm::mat4 view = mCamera.GetViewMatrix();
		glm::mat4 proj = glm::perspective(glm::radians(75.0f), static_cast<float>(swapExtent.width) / swapExtent.height, 0.1f, 100.0f);
		cubemapData.viewProjInverse = glm::inverse(proj * glm::mat4(glm::mat3(view))); // Remove translations

		// Cubemap pass
		commandBuffer.BindPipeline(*mEnvMapPipe);
		commandBuffer.PushConstants(*mEnvMapPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(cubemapData), &cubemapData);
		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex]->Get(), VK_PIPELINE_BIND_POINT_GRAPHICS, mEnvMapPipeLayout->Get(), 0, 1, &mEnvMapSet, 0, nullptr);
		commandBuffer.Draw(3);

		// Forward pass
		GlobalPassData passData{};
		passData.view = view;
		passData.viewProj = proj * view;
		passData.viewProjLight = lightProj * lightView;
		passData.viewInverse = glm::inverse(view);
		passData.lightDir = view * glm::vec4(lightDir, 0.0f);
		mUniformBuffers[mFrameIndex]->SetData(&passData, sizeof(passData));

		const VkDescriptorSet descSets[] = { mGlobalSets[mFrameIndex], mBindlessSet->Get() };
		commandBuffer.BindPipeline(*mPipe);
		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex]->Get(), VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout->Get(), 0, 2, descSets, 0, nullptr);

		ObjectData pushConsts{};
		for (const auto& mesh : mMeshes)
		{
			pushConsts.model = mesh.transform;
			pushConsts.diffuseMapHandle = mBindlessSet->GetOrCreateId(mesh.diffuseMap, mDevice->GetSamplers().TrilinearColor());
			pushConsts.normalMapHandle = mBindlessSet->GetOrCreateId(mesh.normalMap, mDevice->GetSamplers().TrilinearColor());

			commandBuffer.PushConstants(*mPipeLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				sizeof(pushConsts), &pushConsts
			);
			commandBuffer.BindVertexBuffer(*mesh.vertexBuffer);
			commandBuffer.BindIndexBuffer(*mesh.indexBuffer);
			commandBuffer.DrawIndexed(mesh.indexCount);
		}

		// ImGui::ShowDemoWindow();

		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.Get());

		commandBuffer.EndRendering();

		TransitionSwapchainImage( // TODO: use command buffer?
			swapchain.GetImages()[swapchain.GetImageIndex()],
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
	}

	void App::DrawShadowMap(CommandBuffer& commandBuffer, const glm::mat4& lightView, const glm::mat4& lightProj)
	{
		mShadowPass->Begin(commandBuffer);

		ShadowPassData pushConsts{};
		glm::mat4 viewProj = lightProj * lightView;
		for (const auto& mesh : mMeshes)
		{
			pushConsts.mvp = viewProj * mesh.transform;
			commandBuffer.PushConstants(mShadowPass->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, sizeof(pushConsts), &pushConsts);
			commandBuffer.BindVertexBuffer(*mesh.vertexBuffer);
			commandBuffer.BindIndexBuffer(*mesh.indexBuffer);
			commandBuffer.DrawIndexed(mesh.indexCount);
		}

		mShadowPass->End(commandBuffer);
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
		const uint32_t graphicsIndex = mDevice->GetGraphicsIndex();
		mCommandPool = std::make_unique<CommandPool>(*mDevice, graphicsIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		mImmediatePool = std::make_unique<CommandPool>(*mDevice, graphicsIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
	}

	void App::InitDepthBuffer()
	{
		const auto swapExtent = mDevice->GetSwapchain().GetExtent();
		mDepthImage = std::make_unique<Texture2D>(*mDevice, mDevice->GetDepthFormat(),
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, swapExtent.width, swapExtent.height, false);

		RunImmediateCommands([this](CommandBuffer& cmds)
		{
			cmds.Barrier(*mDepthImage,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
		});
	}

	void App::InitPipeline()
	{
		mGlobalLayout = std::make_unique<DescriptorSetLayout>(*mDevice);
		mGlobalLayout->
			AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)	// Lighting
			.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)						// Cubemap
			.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)						// Shadow Map
			.Commit();

		VkPushConstantRange pcRange{};
		pcRange.offset = 0;
		pcRange.size = sizeof(ObjectData);
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		mPipeLayout = std::make_unique<PipelineLayout>(
			*mDevice,
			std::vector<VkDescriptorSetLayout>{ mGlobalLayout->Get(), mBindlessSet->GetSetLayout() },
			std::vector<VkPushConstantRange>{ pcRange }
		);

		Shader shader(*mDevice, "Assets/Shaders/Basic.spv");
		shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain");
		mPipe = std::make_unique<GraphicsPipeline>(*mDevice, *mPipeLayout, shader);
		mPipe->SetVertexInput({ utils::InputBinding(0, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(Vertex) )}, Vertex::GetInputAttributes())
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.AddColorAttachment(mDevice->GetSwapchain().GetFormat())
			.SetDepthAttachment(mDepthImage->GetFormat(), true)
			.Commit();
	}

	void App::InitCommandBuffers()
	{
		mCommandBuffers = mCommandPool->Allocate(MaxFramesInFlight);
	}

	void App::InitSyncPrimitives()
	{
		mAcquireSemaphores.resize(MaxFramesInFlight);
		mRenderSemaphores.resize(MaxFramesInFlight);
		mRenderFences.resize(MaxFramesInFlight);

		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		for (size_t i = 0; i < mDevice->GetSwapchain().GetViews().size(); ++i)
		{
			VK_CHECK(vkCreateSemaphore(mDevice->Get(), &semaphoreInfo, nullptr, &mAcquireSemaphores[i]));
			VK_CHECK(vkCreateSemaphore(mDevice->Get(), &semaphoreInfo, nullptr, &mRenderSemaphores[i]));
		}

		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		for (size_t i = 0; i < MaxFramesInFlight; ++i)
		{
			VK_CHECK(vkCreateFence(mDevice->Get(), &fenceInfo, nullptr, &mRenderFences[i]));
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
		mDiffuseMap = std::shared_ptr<Texture2D>(CreateAndStageTexture("Assets/Textures/brickwall.jpg", VK_FORMAT_R8G8B8A8_SRGB, false));
		mNormalMap = std::shared_ptr<Texture2D>(CreateAndStageTexture("Assets/Textures/brickwall_normal.jpg", VK_FORMAT_R8G8B8A8_UNORM, false));

		// Duck
		{

			auto [duckVertices, duckIndices] = utils::LoadModel("Assets/Models/Duck.gltf");
			Mesh duck = UploadMesh(duckVertices, duckIndices);
			duck.diffuseMap = mDiffuseMap;
			duck.normalMap = mNormalMap;
			duck.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
			duck.transform = glm::scale(duck.transform, glm::vec3(0.01f));
			mMeshes.emplace_back(std::move(duck));
		}

		{
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

			Mesh plane = UploadMesh(planeVertices, planeIndices);
			plane.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
			plane.transform = glm::scale(plane.transform, glm::vec3(5.0f));
			plane.diffuseMap = mDiffuseMap;
			plane.normalMap = mNormalMap;
			mMeshes.emplace_back(std::move(plane));
		}
	}

	void App::InitUniformBuffers()
	{
		mUniformBuffers.reserve(MaxFramesInFlight);
		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			mUniformBuffers.emplace_back(
				std::make_unique<Buffer>(*mDevice, sizeof(GlobalPassData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT)
			);
		}
	}

	static constexpr size_t gCubemapFaces = 6;
	std::array<stbi_uc*, gCubemapFaces> LoadCubemap(
		int& width,
		int& height,
		const std::array<std::filesystem::path, gCubemapFaces>& paths
	)
	{
		int channels;
		std::array<stbi_uc*, gCubemapFaces> cubemapData;
		for (size_t i = 0; i < gCubemapFaces; ++i)
		{
			stbi_uc* data = stbi_load(paths[i].string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
			if (!data)
			{
				std::cerr << "Failed to load cubemap!\n";
			}

			cubemapData[i] = data;
		}
		return cubemapData;
	}

	void App::InitCubemap()
	{
		stbi_set_flip_vertically_on_load(false); // Reversing UV coords using vp^-1, so images will be loaded in correct orientation

		const std::array<std::filesystem::path, gCubemapFaces> skyboxPaths
		{
			"Assets/Textures/Skybox/right.jpg",
			"Assets/Textures/Skybox/left.jpg",
			"Assets/Textures/Skybox/top.jpg",
			"Assets/Textures/Skybox/bottom.jpg",
			"Assets/Textures/Skybox/front.jpg",
			"Assets/Textures/Skybox/back.jpg",
		};

		int width, height;
		const auto cubemapData = LoadCubemap(width, height, skyboxPaths);

		constexpr VkDeviceSize bytesPerPixel = 4;
		const VkDeviceSize faceSize = width * height * bytesPerPixel;
		const VkDeviceSize size = faceSize * gCubemapFaces;

		Buffer staging(*mDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		stbi_uc* mappedData = reinterpret_cast<stbi_uc*>(staging.Map());
		for (size_t i = 0; i < gCubemapFaces; ++i)
			std::memcpy(mappedData + faceSize * i, cubemapData[i], faceSize);

		staging.Unmap();

		for (const auto& data : cubemapData)
			stbi_image_free(data);

		mEnvMap = std::make_unique<TextureCube>(*mDevice, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, width, height);

		RunImmediateCommands([this, &staging](CommandBuffer& cmds)
		{
			cmds.Barrier(*mEnvMap,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
			cmds.Copy(staging, *mEnvMap);
			cmds.Barrier(*mEnvMap,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT_KHR);
		});
		
		// Create environment pipeline
		mEnvMapSetLayout = std::make_unique<DescriptorSetLayout>(*mDevice);
		mEnvMapSetLayout->AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
						.Commit();

		VkPushConstantRange pcRange{};
		pcRange.offset = 0;
		pcRange.size = sizeof(CubemapData);
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		mEnvMapPipeLayout = std::make_unique<PipelineLayout>(
			*mDevice,
			std::vector<VkDescriptorSetLayout>{ mEnvMapSetLayout->Get() },
			std::vector<VkPushConstantRange>{ pcRange }
		);

		Shader shader(*mDevice, "Assets/Shaders/Cubemap.spv");
		shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain");
		mEnvMapPipe = std::make_unique<GraphicsPipeline>(*mDevice, *mEnvMapPipeLayout, shader);
		mEnvMapPipe->SetVertexInput({}, {})
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.AddColorAttachment(mDevice->GetSwapchain().GetFormat())
			.SetDepthAttachment(mDepthImage->GetFormat(), false)
			.Commit();
	}

	void App::InitShadowResources()
	{
		mShadowPass = std::make_unique<ShadowPass>(*mDevice, sizeof(ShadowPassData), VkExtent2D{ 2048, 2048 });
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
		imageWrite.sampler = mDevice->GetSamplers().TrilinearColor();

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
			cubemapInfo.sampler = mDevice->GetSamplers().TrilinearColor();

			VkWriteDescriptorSet cubemapWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			cubemapWrite.descriptorCount = 1;
			cubemapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			cubemapWrite.dstArrayElement = 0;
			cubemapWrite.dstBinding = 1;
			cubemapWrite.dstSet = mGlobalSets[i];
			cubemapWrite.pImageInfo = &cubemapInfo;

			// Shadow Map
			VkDescriptorImageInfo depthMap{};
			depthMap.imageView = mShadowPass->GetMap().GetView();
			depthMap.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
			depthMap.sampler = mDevice->GetSamplers().Shadow();

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

		vkCmdPipelineBarrier2(mCommandBuffers[mFrameIndex]->Get(), &depInfo);
	}

	void App::RunImmediateCommands(const std::function<void(CommandBuffer&)>& cmds)
	{
		auto cmdBuf = mImmediatePool->Allocate();
		cmdBuf->Begin();
		cmds(*cmdBuf);
		cmdBuf->End();
		mDevice->SubmitAndFlush(*cmdBuf);
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

		RunImmediateCommands([&resTex, &stagingTex, generateMipmaps](CommandBuffer& cmds)
		{
			cmds.Barrier(*resTex,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

			cmds.Copy(stagingTex, *resTex);

			if (generateMipmaps)
			{
				cmds.GenerateMipmaps(*resTex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
			}
			else
			{
				cmds.Barrier(*resTex,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
					VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
			}
		});

		return resTex;
	}

	Mesh App::UploadMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		Mesh res;
		Buffer stagingVerts(*mDevice, vertices.size() * sizeof(vertices[0]), vertices.data());
		Buffer stagingIdxs(*mDevice, indices.size() * sizeof(indices[0]), indices.data());
		res.vertexBuffer = std::make_unique<Buffer>(
			*mDevice, stagingVerts.GetSize(),
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
		res.indexBuffer = std::make_unique<Buffer>(
			*mDevice, stagingIdxs.GetSize(),
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
		res.indexCount = indices.size();
		RunImmediateCommands([this, &stagingVerts, &stagingIdxs, &res](CommandBuffer& cmds)
		{
			cmds.Copy(stagingVerts, *res.vertexBuffer);
			cmds.Copy(stagingIdxs, *res.indexBuffer);
		});
		return res;
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

		float deltaX = xpos - lastX;
		float deltaY = lastY - ypos;

		constexpr float sensitivity = 0.2f;
		app->mCamera.yaw += sensitivity * deltaX;
		app->mCamera.pitch += sensitivity * deltaY;
		glfwSetCursorPos(window, lastX, lastY);
	}
}