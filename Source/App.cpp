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
#include <tiny_gltf.h>
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
		InitDescriptorPool();
		InitSyncPrimitives();
		InitImGui();
		InitModel();
		InitVertexBuffer();
		InitIndexBuffer();
		InitUniformBuffers();
		InitTexture();
		InitCubemap();
		InitShadowResources();
		InitDescriptorSets();
	}

	App::~App()
	{
		const auto dev = mDevice->Get();
		const auto allocator = mDevice->GetAllocator();

		mDevice->WaitIdle();

		vkDestroyPipeline(dev, mShadowPipe, nullptr);
		vkDestroyPipelineLayout(dev, mShadowPipeLayout, nullptr);

		vkDestroyPipeline(dev, mEnvMapPipe, nullptr);
		vkDestroyPipelineLayout(dev, mEnvMapPipeLayout, nullptr);
		vkDestroyDescriptorSetLayout(dev, mEnvMapSetLayout, nullptr);

		vkDestroySampler(dev, mShadowMapSampler, nullptr);
		mShadowMap.reset();

		vkDestroySampler(dev, mEnvMapSampler, nullptr);
		mEnvMap.reset();

		vkDestroySampler(dev, mTextureSampler, nullptr);
		mTexture.reset();

		mPlaneIBO.reset();
		mPlaneVBO.reset();

		mUniformBuffers.clear();
		mMeshIBO.reset();
		mMeshVBO.reset();

		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		for (const auto& fence : mRenderFences)
			vkDestroyFence(dev, fence, nullptr);
		
		for (const auto& sem : mRenderSemaphores)
			vkDestroySemaphore(dev, sem, nullptr);

		for (const auto& sem : mAcquireSemaphores)
			vkDestroySemaphore(dev, sem, nullptr);

		vkDestroyDescriptorPool(dev, mPerObjectPool, nullptr);
		vkDestroyDescriptorPool(dev, mGlobalPool, nullptr);

		vkDestroyPipeline(dev, mPipe, nullptr);
		vkDestroyPipelineLayout(dev, mPipeLayout, nullptr);
		vkDestroyDescriptorSetLayout(dev, mPerObjectLayout, nullptr);
		vkDestroyDescriptorSetLayout(dev, mGlobalLayout, nullptr);

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
		vkCmdBindPipeline(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mEnvMapPipe);
		vkCmdPushConstants(mCommandBuffers[mFrameIndex], mEnvMapPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(cubemapData), &cubemapData);
		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mEnvMapPipeLayout, 0, 1, &mEnvMapSet, 0, nullptr);
		vkCmdDraw(mCommandBuffers[mFrameIndex], 3, 1, 0, 0);

		// Forward pass
		LightingData lighting{};
		lighting.vInverse = glm::inverse(view);
		lighting.lightDir = view * glm::vec4(lightDir, 0.0f);

		void* globalBufferData = mUniformBuffers[mFrameIndex]->Map();
		std::memcpy(globalBufferData, &lighting, sizeof(lighting));
		mUniformBuffers[mFrameIndex]->Unmap();

		vkCmdBindPipeline(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipe);

		VkDescriptorSet descSets[] = { mGlobalSets[mFrameIndex], mPerObjectSet };
		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout, 0, 2, descSets, 0, nullptr);

		MatrixData pushConsts{};
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, -1.0f, 0.0f));
		model = glm::scale(model, glm::vec3(0.01f));

		pushConsts.mv = view * model;
		pushConsts.mvp = proj * pushConsts.mv;
		pushConsts.normal = glm::transpose(glm::inverse(pushConsts.mv));
		pushConsts.mvpLight = lightProj * lightView * model;
		vkCmdPushConstants(mCommandBuffers[mFrameIndex], mPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);
		
		// Mesh
		VkDeviceSize offsets[] = { 0 };
		VkBuffer vertexBuffer = mMeshVBO->Get();
		vkCmdBindVertexBuffers(mCommandBuffers[mFrameIndex], 0, 1, &vertexBuffer, offsets);
		vkCmdBindIndexBuffer(mCommandBuffers[mFrameIndex], mMeshIBO->Get(), 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(mCommandBuffers[mFrameIndex], mIndices.size(), 1, 0, 0, 0);

		// Plane
		model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, -1.0f, 0.0f));
		model = glm::scale(model, glm::vec3(5.0f));

		pushConsts.mv = view * model;
		pushConsts.mvp = proj * pushConsts.mv;
		pushConsts.normal = glm::transpose(glm::inverse(pushConsts.mv));
		pushConsts.mvpLight = lightProj * lightView * model;
		vkCmdPushConstants(mCommandBuffers[mFrameIndex], mPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);

		vertexBuffer = mPlaneVBO->Get();
		vkCmdBindVertexBuffers(mCommandBuffers[mFrameIndex], 0, 1, &vertexBuffer, offsets);
		vkCmdBindIndexBuffer(mCommandBuffers[mFrameIndex], mPlaneIBO->Get(), 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(mCommandBuffers[mFrameIndex], 6, 1, 0, 0, 0);

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

		VkClearValue depthClear{};
		depthClear.depthStencil.depth = 1.0f;
		depthClear.depthStencil.stencil = 0;

		VkRenderingAttachmentInfo shadowAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		shadowAttachment.clearValue = depthClear;
		shadowAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		shadowAttachment.imageView = mShadowMap->GetView();
		shadowAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		shadowAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.pDepthAttachment = &shadowAttachment;
		renderingInfo.renderArea = shadowRenderArea;
		renderingInfo.layerCount = 1;

		vkCmdBeginRendering(commandBuffer, &renderingInfo);

		{
			const auto [viewport, scissor] = utils::ViewportAndScissor(shadowMapExtent);
			vkCmdSetViewport(commandBuffer, 0, 1, & viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipe);

			MatrixData pushConsts{};
			glm::mat4 model = glm::mat4(1.0f);
			model = glm::translate(model, glm::vec3(0.0f, -1.0f, 0.0f));
			model = glm::scale(model, glm::vec3(0.01f));
			pushConsts.mvp = lightProj * lightView * model;

			vkCmdPushConstants(commandBuffer, mShadowPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);

			// Mesh
			VkDeviceSize offsets[] = { 0 };
			VkBuffer vertexBuffer = mMeshVBO->Get();
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
			vkCmdBindIndexBuffer(commandBuffer, mMeshIBO->Get(), 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandBuffer, mIndices.size(), 1, 0, 0, 0);

			// Plane
			model = glm::mat4(1.0f);
			model = glm::translate(model, glm::vec3(0.0f, -1.0f, 0.0f));
			model = glm::scale(model, glm::vec3(5.0f));

			pushConsts.mvp = lightProj * lightView * model;
			vkCmdPushConstants(commandBuffer, mShadowPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);

			vertexBuffer = mPlaneVBO->Get();
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
			vkCmdBindIndexBuffer(commandBuffer, mPlaneIBO->Get(), 0, VK_INDEX_TYPE_UINT16);
			vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);
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

		std::array<VkPipelineShaderStageCreateInfo, 2> stages =
		{
			MakeShaderStage(shader, VK_SHADER_STAGE_VERTEX_BIT, "VSMain"),
			MakeShaderStage(shader, VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain")
		};

		std::array<VkDynamicState, 2> dynamicStates =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		std::array<VkVertexInputBindingDescription, 1> inputBindings;
		inputBindings[0].binding = 0;
		inputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		inputBindings[0].stride = sizeof(Vertex);

		std::array<VkVertexInputAttributeDescription, 4> inputAttribs;
		inputAttribs[0] = utils::InputAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
		inputAttribs[1] = utils::InputAttribute(0, 1, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 3);
		inputAttribs[2] = utils::InputAttribute(0, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 7);
		inputAttribs[3] = utils::InputAttribute(0, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 9);

		VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInput.vertexBindingDescriptionCount = inputBindings.size();
		vertexInput.pVertexBindingDescriptions = inputBindings.data();
		vertexInput.vertexAttributeDescriptionCount = inputAttribs.size();
		vertexInput.pVertexAttributeDescriptions = inputAttribs.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.sampleShadingEnable = VK_FALSE;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorAttachment{};
		colorAttachment.blendEnable = VK_FALSE;
		colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo colorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlend.attachmentCount = 1;
		colorBlend.pAttachments = &colorAttachment;
		colorBlend.logicOpEnable = VK_FALSE;

		VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

		VkPushConstantRange matrixRange{};
		matrixRange.offset = 0;
		matrixRange.size = sizeof(MatrixData);
		matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkDescriptorSetLayoutBinding lightingBufferBinding{};
		lightingBufferBinding.binding = 0;
		lightingBufferBinding.descriptorCount = 1;
		lightingBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		lightingBufferBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding cubemapBinding{};
		cubemapBinding.binding = 1;
		cubemapBinding.descriptorCount = 1;
		cubemapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		cubemapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding shadowMapBinding{};
		shadowMapBinding.binding = 2;
		shadowMapBinding.descriptorCount = 1;
		shadowMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		shadowMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding imageBinding{};
		imageBinding.binding = 0;
		imageBinding.descriptorCount = 1;
		imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		imageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding globalBindings[]{ lightingBufferBinding, cubemapBinding, shadowMapBinding };
		VkDescriptorSetLayoutCreateInfo globalLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		globalLayoutInfo.bindingCount = 3;
		globalLayoutInfo.pBindings = globalBindings;

		VkDescriptorSetLayoutCreateInfo perObjectLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		perObjectLayoutInfo.bindingCount = 1;
		perObjectLayoutInfo.pBindings = &imageBinding;

		VK_CHECK(vkCreateDescriptorSetLayout(dev, &globalLayoutInfo, nullptr, &mGlobalLayout));
		VK_CHECK(vkCreateDescriptorSetLayout(dev, &perObjectLayoutInfo, nullptr, &mPerObjectLayout));

		VkDescriptorSetLayout setLayouts[] = { mGlobalLayout, mPerObjectLayout };

		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &matrixRange;
		layoutInfo.setLayoutCount = 2;
		layoutInfo.pSetLayouts = setLayouts;
		
		VK_CHECK(vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &mPipeLayout));

		const auto format = mDevice->GetSwapchain().GetFormat();
		VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &format;
		renderingInfo.depthAttachmentFormat = mDepthImage->GetFormat();

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.pNext = &renderingInfo;
		pipelineInfo.stageCount = stages.size();
		pipelineInfo.pStages = stages.data();
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewport;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.layout = mPipeLayout;

		VK_CHECK(vkCreateGraphicsPipelines(dev, mDevice->GetPipelineCache(), 1, &pipelineInfo, nullptr, &mPipe));

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

	void App::InitDescriptorPool()
	{
		const auto dev = mDevice->Get();

		VkDescriptorPoolSize globalSizes[]{ { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * MaxFramesInFlight + 1 } };
		VkDescriptorPoolSize perObjectSizes[]{ { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };

		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = 2 * MaxFramesInFlight + 1;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes = globalSizes;
		VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &mGlobalPool));
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = perObjectSizes;
		VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &mPerObjectPool));
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

	void App::InitModel()
	{
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;

		std::string error, warn;

		constexpr std::string_view modelPath = "Assets/Models/Duck.gltf";

		bool res = loader.LoadASCIIFromFile(&model, &error, &warn, modelPath.data());
		if (!warn.empty())
			std::cerr << "GLTF warning: " << warn << '\n';

		if (!error.empty())
			std::cerr << "GLTF error: " << error << '\n';

		if (!res)
		{
			std::cerr << "Failed to load model from path " << modelPath << "!\n";
			return;
		}

		std::unordered_map<Vertex, uint32_t> uniqueVertices;
		for (const auto& mesh : model.meshes)
		{
			for (const auto& prim : mesh.primitives)
			{
				// Indices
				const tinygltf::Accessor& indexAccessor = model.accessors[prim.indices];
				const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
				const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

				// Vertex positions
				const tinygltf::Accessor& posAccessor = model.accessors[prim.attributes.at("POSITION")];
				const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
				const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

				bool hasTexCoord = prim.attributes.find("TEXCOORD_0") != prim.attributes.end();
				const tinygltf::Accessor* texCoordAccessor = nullptr;
				const tinygltf::BufferView* texCoordBufferView = nullptr;
				const tinygltf::Buffer* texCoordBuffer = nullptr;

				// Normals
				const tinygltf::Accessor& normalAccessor = model.accessors[prim.attributes.at("NORMAL")];
				const tinygltf::BufferView& normalBufferView = model.bufferViews[normalAccessor.bufferView];
				const tinygltf::Buffer& normalBuffer = model.buffers[normalBufferView.buffer];

				if (hasTexCoord)
				{
					texCoordAccessor = &model.accessors[prim.attributes.at("TEXCOORD_0")];
					texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
					texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
				}

				for (size_t i = 0; i < posAccessor.count; ++i)
				{
					Vertex v{};
					const float* pos = reinterpret_cast<const float*>(&posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset + i * 12]);
					v.position = { pos[0], pos[1], pos[2] };

					if (hasTexCoord)
					{
						const float* uv = reinterpret_cast<const float*>(&texCoordBuffer->data[texCoordBufferView->byteOffset + texCoordAccessor->byteOffset + i * 8]);
						v.uv = { uv[0], 1.0f - uv[1] };
					}

					v.color = glm::vec4(1.0f);

					const float* normal = reinterpret_cast<const float*>(&normalBuffer.data[normalBufferView.byteOffset + normalAccessor.byteOffset + i * 12]);
					v.normal = { normal[0], normal[1], normal[2] };

					if (uniqueVertices.find(v) == uniqueVertices.end())
					{
						uniqueVertices[v] = static_cast<uint32_t>(mVertices.size());
						mVertices.push_back(v);
					}
				}

				const uint8_t* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
				if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					const uint16_t* indices = reinterpret_cast<const uint16_t*>(indexData);
					for (size_t i = 0; i < indexAccessor.count; ++i)
					{
						Vertex v = mVertices[indices[i]];
						mIndices.emplace_back(uniqueVertices[v]);
					}
				}
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
				{
					const uint32_t* indices = reinterpret_cast<const uint32_t*>(indexData);
					for (size_t i = 0; i < indexAccessor.count; ++i)
					{
						Vertex v = mVertices[indices[i]];
						mIndices.push_back(uniqueVertices[v]);
					}
				}
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
				{
					for (size_t i = 0; i < indexAccessor.count; ++i)
					{
						Vertex v = mVertices[indexData[i]];
						mIndices.emplace_back(uniqueVertices[v]);
					}
				}
			}
		}
	}

	void App::InitVertexBuffer()
	{
		auto cmds = CreateImmediateCommandBuffer();
		
		const VkDeviceSize meshSize = mVertices.size() * sizeof(mVertices[0]);
		
		const std::vector<Vertex> planeVertices
		{
			{ { -0.5f, 0.0f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.0f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { -0.5f, 0.0f, -0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		};

		Buffer stagingMesh(*mDevice, meshSize, mVertices.data());
		mMeshVBO = std::make_unique<Buffer>(*mDevice, meshSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0);
		CopyBuffer(cmds, stagingMesh.Get(), mMeshVBO->Get(), meshSize);

		const VkDeviceSize planeSize = planeVertices.size() * sizeof(planeVertices[0]);
		Buffer stagingPlane(*mDevice, meshSize, planeVertices.data());
		mPlaneVBO = std::make_unique<Buffer>(*mDevice, planeSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0);
		CopyBuffer(cmds, stagingPlane.Get(), mPlaneVBO->Get(), planeSize);
		
		SubmitImmediateCommandBuffer(cmds);
	}

	void App::InitIndexBuffer()
	{
		const VkDeviceSize meshSize = mIndices.size() * sizeof(mIndices[0]);
		const std::vector<uint16_t> planeIndices
		{
			0, 1, 2,
			2, 3, 0
		};
		const VkDeviceSize planeSize = planeIndices.size() * sizeof(planeIndices[0]);

		auto cmds = CreateImmediateCommandBuffer();

		Buffer stagingMesh(*mDevice, meshSize, mIndices.data());
		mMeshIBO = std::make_unique<Buffer>(*mDevice, meshSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0);
		CopyBuffer(cmds, stagingMesh.Get(), mMeshIBO->Get(), meshSize);

		Buffer stagingPlane(*mDevice, planeSize, planeIndices.data());
		mPlaneIBO = std::make_unique<Buffer>(*mDevice, planeSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0);
		CopyBuffer(cmds, stagingPlane.Get(), mPlaneIBO->Get(), planeSize);

		SubmitImmediateCommandBuffer(cmds);
	}

	void App::InitUniformBuffers()
	{
		const auto size = sizeof(LightingData);

		mUniformBuffers.reserve(MaxFramesInFlight);
		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			mUniformBuffers.emplace_back(
				std::make_unique<Buffer>(*mDevice, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT));
		}
	}

	void App::InitTexture()
	{
		stbi_set_flip_vertically_on_load(true);

		const auto dev = mDevice->Get();
		const auto allocator = mDevice->GetAllocator();
		constexpr VkDeviceSize bytesPerPixel = 4;

		// Load texture image
		ktxTexture* texture;
		KTX_error_code res = ktxTexture_CreateFromNamedFile("Assets/Textures/brickwall.ktx", KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
		if (res != KTX_SUCCESS)
		{
			std::cerr << "Failed to load KTX texture!\n";
			return;
		}

		uint32_t width = texture->baseWidth;
		uint32_t height = texture->baseHeight;
		ktx_size_t size = ktxTexture_GetImageSize(texture, 0);
		ktx_uint8_t* data = ktxTexture_GetData(texture);

		Buffer staging(*mDevice, size, data);

		ktxTexture_Destroy(texture);

		mTexture = std::make_unique<Texture2D>(
			*mDevice, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			width, height, true);

		auto cmds = CreateImmediateCommandBuffer();

		mTexture->Barrier(cmds,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

		VkBufferImageCopy buffer2Image = mTexture->CopyFromBuffer();
		vkCmdCopyBufferToImage(cmds, staging.Get(), mTexture->Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer2Image);

		mTexture->GenerateMipmaps(cmds, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
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

		VK_CHECK(vkCreateSampler(dev, &samplerInfo, nullptr, &mTextureSampler));
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

		auto shaderSource = utils::ReadFile("Assets/Shaders/Cubemap.spv");
		VkShaderModule shader = CreateShader(shaderSource);

		std::array<VkPipelineShaderStageCreateInfo, 2> stages =
		{
			MakeShaderStage(shader, VK_SHADER_STAGE_VERTEX_BIT, "VSMain"),
			MakeShaderStage(shader, VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain")
		};

		std::array<VkDynamicState, 2> dynamicStates =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInput.vertexBindingDescriptionCount = 0;
		vertexInput.vertexAttributeDescriptionCount = 0;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.sampleShadingEnable = VK_FALSE;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorAttachment{};
		colorAttachment.blendEnable = VK_FALSE;
		colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo colorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlend.attachmentCount = 1;
		colorBlend.pAttachments = &colorAttachment;
		colorBlend.logicOpEnable = VK_FALSE;

		VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_FALSE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

		VkDescriptorSetLayoutBinding cubemapBinding{};
		cubemapBinding.binding = 0;
		cubemapBinding.descriptorCount = 1;
		cubemapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		cubemapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo cubemapLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		cubemapLayoutInfo.bindingCount = 1;
		cubemapLayoutInfo.pBindings = &cubemapBinding;

		VK_CHECK(vkCreateDescriptorSetLayout(dev, &cubemapLayoutInfo, nullptr, &mEnvMapSetLayout));

		VkPushConstantRange pcRange{};
		pcRange.offset = 0;
		pcRange.size = sizeof(CubemapData);
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &mEnvMapSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pcRange;

		VK_CHECK(vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &mEnvMapPipeLayout));

		const auto format = mDevice->GetSwapchain().GetFormat();
		VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &format;
		renderingInfo.depthAttachmentFormat = mDepthImage->GetFormat();

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.pNext = &renderingInfo;
		pipelineInfo.stageCount = stages.size();
		pipelineInfo.pStages = stages.data();
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewport;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.layout = mEnvMapPipeLayout;

		VK_CHECK(vkCreateGraphicsPipelines(dev, mDevice->GetPipelineCache(), 1, &pipelineInfo, nullptr, &mEnvMapPipe));

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

		std::array<VkPipelineShaderStageCreateInfo, 1> stages =
		{
			MakeShaderStage(shader, VK_SHADER_STAGE_VERTEX_BIT, "VSMain"),
		};

		std::array<VkDynamicState, 2> dynamicStates =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		std::array<VkVertexInputBindingDescription, 1> inputBindings;
		inputBindings[0].binding = 0;
		inputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		inputBindings[0].stride = sizeof(Vertex);

		std::array<VkVertexInputAttributeDescription, 1> inputAttribs;
		inputAttribs[0] = utils::InputAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);

		VkPipelineVertexInputStateCreateInfo vertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInput.vertexBindingDescriptionCount = inputBindings.size();
		vertexInput.pVertexBindingDescriptions = inputBindings.data();
		vertexInput.vertexAttributeDescriptionCount = inputAttribs.size();
		vertexInput.pVertexAttributeDescriptions = inputAttribs.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
		multisample.sampleShadingEnable = VK_FALSE;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendStateCreateInfo colorBlend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
		colorBlend.attachmentCount = 0;
		colorBlend.logicOpEnable = VK_FALSE;

		VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

		VkPushConstantRange matrixRange{};
		matrixRange.offset = 0;
		matrixRange.size = sizeof(MatrixData);
		matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &matrixRange;

		VK_CHECK(vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &mShadowPipeLayout));

		const auto format = mDevice->GetSwapchain().GetFormat();
		VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.depthAttachmentFormat = mShadowMap->GetFormat();

		VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
		pipelineInfo.pNext = &renderingInfo;
		pipelineInfo.stageCount = stages.size();
		pipelineInfo.pStages = stages.data();
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewport;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pColorBlendState = &colorBlend;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.layout = mShadowPipeLayout;

		VK_CHECK(vkCreateGraphicsPipelines(dev, mDevice->GetPipelineCache(), 1, &pipelineInfo, nullptr, &mShadowPipe));

		vkDestroyShaderModule(dev, shader, nullptr);
	}

	void App::InitDescriptorSets()
	{
		const auto dev = mDevice->Get();

		mGlobalSets.resize(MaxFramesInFlight);
		VkDescriptorSetLayout globalLayouts[]{ mGlobalLayout, mGlobalLayout };
		VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocInfo.descriptorPool = mGlobalPool;
		allocInfo.descriptorSetCount = MaxFramesInFlight;
		allocInfo.pSetLayouts = globalLayouts;
		VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, mGlobalSets.data()));

		allocInfo.descriptorPool = mPerObjectPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mPerObjectLayout;
		VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &mPerObjectSet));

		allocInfo.descriptorPool = mGlobalPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mEnvMapSetLayout;
		VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &mEnvMapSet));

		VkDescriptorImageInfo imageWrite{};
		imageWrite.imageView = mTexture->GetView();
		imageWrite.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageWrite.sampler = mTextureSampler;

		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.dstArrayElement = 0;
		write.dstBinding = 0;
		write.dstSet = mPerObjectSet;
		write.pImageInfo = &imageWrite;
		vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

		imageWrite.imageView = mEnvMap->GetView();
		imageWrite.sampler = mEnvMapSampler;

		write.dstSet = mEnvMapSet;
		vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

		for (int i = 0; i < MaxFramesInFlight; ++i)
		{
			VkDescriptorBufferInfo buffer{};
			buffer.buffer = mUniformBuffers[i]->Get();
			buffer.offset = 0;
			buffer.range = sizeof(LightingData);

			VkWriteDescriptorSet bufferWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			bufferWrite.descriptorCount = 1;
			bufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bufferWrite.dstArrayElement = 0;
			bufferWrite.dstBinding = 0;
			bufferWrite.dstSet = mGlobalSets[i];
			bufferWrite.pBufferInfo = &buffer;

			// Cubemap
			write.dstBinding = 1;
			write.dstSet = mGlobalSets[i];

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

			VkWriteDescriptorSet writes[]{ bufferWrite, write, depthMapWrite };
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

	VkPipelineShaderStageCreateInfo App::MakeShaderStage(VkShaderModule shader, VkShaderStageFlagBits stage, const char* entrypoint)
	{
		VkPipelineShaderStageCreateInfo res{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		res.module = shader;
		res.pName = entrypoint;
		res.stage = stage;
		return res;
	}
}