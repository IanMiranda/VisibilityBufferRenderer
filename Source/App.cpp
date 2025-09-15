#include "App.h"

#include <array>
#include <optional>
#include <algorithm>
#include <fstream>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <tiny_obj_loader.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_glfw.h>

#include "Utils.h"

namespace im
{
	App::App()
	{
		InitWindow();

		mDevice = std::make_unique<Device>(mWindow);

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
		InitDescriptorSets();
	}

	App::~App()
	{
		const auto dev = mDevice->Get();
		const auto allocator = mDevice->GetAllocator();

		mDevice->WaitIdle();

		vkDestroyPipeline(dev, mCubemapPipe, nullptr);
		vkDestroyPipelineLayout(dev, mCubemapPipeLayout, nullptr);
		vkDestroyDescriptorSetLayout(dev, mCubemapSetLayout, nullptr);

		vkDestroySampler(dev, mCubemapSampler, nullptr);
		mCubemap.reset();

		vkDestroySampler(dev, mTextureSampler, nullptr);
		mTexture.reset();

		mUniformBuffers.clear();
		mIndexBuffer.reset();
		mVertexBuffer.reset();

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
		//glm::mat4 view = glm::lookAt(glm::vec3(2.0f * sinf(deltaTime), 0.0f, 2.0f * cosf(deltaTime)), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		if (glfwGetKey(mWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(mWindow, GLFW_TRUE);

		const float moveFactor = 2.5f;

		const auto front = mCamera.GetFront();
		glm::vec3 up(0.0f, 1.0f, 0.0f);
		glm::vec3 right = glm::normalize(glm::cross(front, up));

		if (glfwGetKey(mWindow, GLFW_KEY_W) == GLFW_PRESS)
		{
			mCamera.SetPosition(mCamera.GetPosition() + front * deltaTime * moveFactor);
		}
		else if (glfwGetKey(mWindow, GLFW_KEY_S) == GLFW_PRESS)
		{
			mCamera.SetPosition(mCamera.GetPosition() + -front * deltaTime * moveFactor);
		}

		if (glfwGetKey(mWindow, GLFW_KEY_A) == GLFW_PRESS)
		{
			mCamera.SetPosition(mCamera.GetPosition() + -right * deltaTime * moveFactor);
		}
		else if (glfwGetKey(mWindow, GLFW_KEY_D) == GLFW_PRESS)
		{
			mCamera.SetPosition(mCamera.GetPosition() + right * deltaTime * moveFactor);
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

		TransitionSwapchainImage(
			swapchain.GetImages()[imageIndex],
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_ACCESS_2_NONE,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

		VkClearValue clearColor{};
		clearColor.color = { 0.0f, 0.0f, 0.0f, 1.0f };

		VkRect2D area{};
		area.offset = { 0, 0 };
		area.extent = swapExtent;

		VkRenderingAttachmentInfo colorAttachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachmentInfo.clearValue = clearColor;
		colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachmentInfo.imageView = swapchain.GetViews()[imageIndex];
		colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		VkClearValue clearDepth{};
		clearDepth.depthStencil.depth = 1.0f;
		clearDepth.depthStencil.stencil = 0;

		VkRenderingAttachmentInfo depthAttachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthAttachmentInfo.clearValue = clearDepth;
		depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachmentInfo.imageView = mDepthImage->GetView();
		depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachmentInfo;
		renderingInfo.pDepthAttachment = &depthAttachmentInfo;
		renderingInfo.renderArea = area;
		renderingInfo.layerCount = 1;

		vkCmdBeginRendering(mCommandBuffers[mFrameIndex], &renderingInfo);

		VkViewport viewport{};
		viewport.width = swapExtent.width;
		viewport.height = -static_cast<float>(swapExtent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		viewport.x = 0.0f;
		viewport.y = static_cast<float>(swapExtent.height);

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = swapExtent;

		vkCmdSetViewport(mCommandBuffers[mFrameIndex], 0, 1, &viewport);
		vkCmdSetScissor(mCommandBuffers[mFrameIndex], 0, 1, &scissor);

		CubemapData cubemapData{};
		glm::mat4 view = mCamera.GetViewMatrix();
		glm::mat4 proj = glm::perspective(glm::radians(75.0f), static_cast<float>(swapExtent.width) / swapExtent.height, 0.1f, 100.0f);
		cubemapData.vpInverse = glm::inverse(proj * glm::mat4(glm::mat3(view))); // Remove translations

		// Cubemap pass
		vkCmdBindPipeline(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mCubemapPipe);
		vkCmdPushConstants(mCommandBuffers[mFrameIndex], mCubemapPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(cubemapData), &cubemapData);
		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mCubemapPipeLayout, 0, 1, &mCubemapSet, 0, nullptr);
		vkCmdDraw(mCommandBuffers[mFrameIndex], 3, 1, 0, 0);

		// Forward pass
		vkCmdBindPipeline(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipe);

		MatrixData pushConsts{};
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, -1.0f, 0.0f));
		model = glm::scale(model, glm::vec3(0.1f));

		pushConsts.mv = view * model;
		pushConsts.mvp = proj * pushConsts.mv;
		pushConsts.normal = glm::transpose(glm::inverse(view * model));

		LightingData lighting{};
		lighting.vInverse = glm::inverse(view);
		lighting.lightPosition = view * glm::vec4(0.0f, 1.0f, 5.0f, 1.0f);

		void* globalBufferData = mUniformBuffers[mFrameIndex]->Map();
		std::memcpy(globalBufferData, &lighting, sizeof(lighting));
		mUniformBuffers[mFrameIndex]->Unmap();

		VkDescriptorSet descSets[] = { mGlobalSets[mFrameIndex], mPerObjectSet };
		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout, 0, 2, descSets, 0, nullptr);

		vkCmdPushConstants(mCommandBuffers[mFrameIndex], mPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);
		
		VkDeviceSize offsets[] = { 0 };
		VkBuffer vertexBuffer = mVertexBuffer->Get();
		vkCmdBindVertexBuffers(mCommandBuffers[mFrameIndex], 0, 1, &vertexBuffer, offsets);

		vkCmdBindIndexBuffer(mCommandBuffers[mFrameIndex], mIndexBuffer->Get(), 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(mCommandBuffers[mFrameIndex], mIndices.size(), 1, 0, 0, 0);

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
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
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

		VkDescriptorSetLayoutBinding imageBinding{};
		imageBinding.binding = 0;
		imageBinding.descriptorCount = 1;
		imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		imageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutBinding globalBindings[]{ lightingBufferBinding, cubemapBinding };
		VkDescriptorSetLayoutCreateInfo globalLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		globalLayoutInfo.bindingCount = 2;
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

		VK_CHECK(vkCreateGraphicsPipelines(dev, nullptr, 1, &pipelineInfo, nullptr, &mPipe));

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

		VkDescriptorPoolSize globalSizes[]{ { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxFramesInFlight }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxFramesInFlight + 1 } };
		VkDescriptorPoolSize perObjectSizes[]{ { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };

		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = MaxFramesInFlight + 1;
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
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn;
		std::string error;
		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &error, "Assets/Models/teapot.obj"))
		{
			std::cerr << "Failed to load model: " << warn << error << '\n';
		}

		std::unordered_map<Vertex, uint32_t> uniqueVertices;

		for (const auto& shape : shapes)
		{
			for (const auto& index : shape.mesh.indices)
			{
				Vertex v{};
				v.position = glm::vec3(
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2]);
				v.uv = glm::vec2(
					attrib.texcoords[2 * index.texcoord_index + 0],
					attrib.texcoords[2 * index.texcoord_index + 1]);
				v.color = glm::vec4(1.0f);
				v.normal = glm::vec3(
					attrib.normals[3 * index.normal_index + 0],
					attrib.normals[3 * index.normal_index + 1],
					attrib.normals[3 * index.normal_index + 2]);
				
				if (uniqueVertices.count(v) == 0)
				{
					uniqueVertices[v] = static_cast<uint32_t>(mVertices.size());
					mVertices.push_back(v);
				}

				mIndices.push_back(uniqueVertices[v]);
			}
		}
	}

	void App::InitVertexBuffer()
	{
		const auto size = mVertices.size() * sizeof(mVertices[0]);
		
		Buffer staging(*mDevice, size, mVertices.data());

		mVertexBuffer = std::make_unique<Buffer>(*mDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0);

		auto cmds = CreateImmediateCommandBuffer();
		CopyBuffer(cmds, staging.Get(), mVertexBuffer->Get(), size);
		SubmitImmediateCommandBuffer(cmds);
	}

	void App::InitIndexBuffer()
	{
		const auto size = mIndices.size() * sizeof(mIndices[0]);

		Buffer staging(*mDevice, size, mIndices.data());

		mIndexBuffer = std::make_unique<Buffer>(*mDevice, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0);

		auto cmds = CreateImmediateCommandBuffer();
		CopyBuffer(cmds, staging.Get(), mIndexBuffer->Get(), size);
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

		int width, height, channels;
		stbi_uc* data = stbi_load("Assets/Textures/teapot-porcelain.jpg", &width, &height, &channels, STBI_rgb_alpha);
		if (!data)
		{
			std::cerr << "Failed to load texture image!\n";
		}
		
		const VkDeviceSize size = width * height * bytesPerPixel;

		Buffer staging(*mDevice, size, data);

		stbi_image_free(data);

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

		mCubemap = std::make_unique<TextureCube>(*mDevice, VK_FORMAT_R8G8B8A8_SRGB,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, width, height);

		auto cmds = CreateImmediateCommandBuffer();

		mCubemap->Barrier(cmds,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

		VkBufferImageCopy buffer2Image = mCubemap->CopyFromBuffer();
		vkCmdCopyBufferToImage(cmds, staging.Get(), mCubemap->Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer2Image);

		mCubemap->Barrier(cmds,
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

		VK_CHECK(vkCreateSampler(dev, &samplerInfo, nullptr, &mCubemapSampler));

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

		VK_CHECK(vkCreateDescriptorSetLayout(dev, &cubemapLayoutInfo, nullptr, &mCubemapSetLayout));

		VkPushConstantRange pcRange{};
		pcRange.offset = 0;
		pcRange.size = sizeof(CubemapData);
		pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &mCubemapSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pcRange;

		VK_CHECK(vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &mCubemapPipeLayout));

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
		pipelineInfo.layout = mCubemapPipeLayout;

		VK_CHECK(vkCreateGraphicsPipelines(dev, nullptr, 1, &pipelineInfo, nullptr, &mCubemapPipe));

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
		allocInfo.pSetLayouts = &mCubemapSetLayout;
		VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &mCubemapSet));

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

		imageWrite.imageView = mCubemap->GetView();
		imageWrite.sampler = mCubemapSampler;

		write.dstSet = mCubemapSet;
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

			write.dstBinding = 1;
			write.dstSet = mGlobalSets[i];

			VkWriteDescriptorSet writes[]{ bufferWrite, write };
			vkUpdateDescriptorSets(dev, 2, writes, 0, nullptr);
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
		static double lastX;
		static double lastY;
		App* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
		if (app->mFirstTouch)
		{
			app->mFirstTouch = false;
			lastX = xpos;
			lastY = ypos;
		}

		constexpr float sensitivity = 0.2f;
		float deltaX = xpos - lastX;
		float deltaY = lastY - ypos;

		app->mCamera.SetYaw(app->mCamera.GetYaw() + sensitivity * deltaX);
		app->mCamera.SetPitch(app->mCamera.GetPitch() + sensitivity * deltaY);
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