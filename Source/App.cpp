#include "App.h"

#include <array>
#include <optional>
#include <algorithm>
#include <fstream>
#include <cassert>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <tiny_obj_loader.h>

namespace im
{
#ifndef NDEBUG
	constexpr bool enableValidationLayers = true;
#else
	constexpr bool enableValidationLayers = false;
#endif

	App::App()
	{
		InitWindow();
		InitInstance();
		InitSurface();
		InitDevice();
		InitSwapchain();
		InitPipeline();
		InitCommandPool();
		InitCommandBuffers();
		InitMsaaTarget();
		InitDepthBuffer();
		InitDescriptorPool();
		InitSyncPrimitives();
		InitModel();
		InitVertexBuffer();
		InitIndexBuffer();
		InitTexture();
		InitDescriptorSets();
	}

	App::~App()
	{
		VK_CHECK(vkDeviceWaitIdle(mDevice));

		vkDestroySampler(mDevice, mTextureSampler, nullptr);
		vkDestroyImageView(mDevice, mTextureView, nullptr);
		vmaDestroyImage(mAllocator, mTexture, mTextureAllocation);

		vmaDestroyBuffer(mAllocator, mIndexBuffer, mIndexBufferAllocation);
		vmaDestroyBuffer(mAllocator, mVertexBuffer, mVertexBufferAllocation);

		for (const auto& fence : mRenderFences)
			vkDestroyFence(mDevice, fence, nullptr);
		
		for (const auto& sem : mRenderSemaphores)
			vkDestroySemaphore(mDevice, sem, nullptr);

		for (const auto& sem : mAcquireSemaphores)
			vkDestroySemaphore(mDevice, sem, nullptr);

		vkDestroyDescriptorPool(mDevice, mDescPool, nullptr);

		vkDestroyCommandPool(mDevice, mTransientPool, nullptr);
		vkDestroyCommandPool(mDevice, mCommandPool, nullptr);

		vkDestroyPipeline(mDevice, mPipe, nullptr);
		vkDestroyPipelineLayout(mDevice, mPipeLayout, nullptr);
		vkDestroyDescriptorSetLayout(mDevice, mSetLayout, nullptr);

		CleanupSwapchain();

		vmaDestroyAllocator(mAllocator);
		vkDestroyDevice(mDevice, nullptr);
		vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
		
		if constexpr (enableValidationLayers)
		{
			auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(mInstance, "vkDestroyDebugUtilsMessengerEXT"));
			
			if (destroy)
				destroy(mInstance, mDebugMessenger, nullptr);
			else
				std::cerr << "Failed to load vkDestroyDebugUtilsMessengerEXT!\n";
		}

		vkDestroyInstance(mInstance, nullptr);

		glfwTerminate();
	}

	void App::Run()
	{
		while (!glfwWindowShouldClose(mWindow))
		{
			glfwPollEvents();
			Render();
		}
	}

	void App::Render()
	{
		VK_CHECK(vkWaitForFences(mDevice, 1, &mRenderFences[mFrameIndex], VK_TRUE, UINT64_MAX));

		uint32_t imageIndex;
		VkResult res = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, mAcquireSemaphores[mSemaphoreIndex], nullptr, &imageIndex);
		if (res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RecreateSwapchain();
			return;
		}
		else
		{
			VK_CHECK(res);
		}

		VK_CHECK(vkResetFences(mDevice, 1, &mRenderFences[mFrameIndex]));

		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		
		VK_CHECK(vkBeginCommandBuffer(mCommandBuffers[mFrameIndex], &beginInfo));

		TransitionSwapchainImage(
			imageIndex,
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
		area.extent = mSwapchainExtent;

		VkRenderingAttachmentInfo colorAttachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachmentInfo.clearValue = clearColor;
		colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachmentInfo.imageView = mMsaaView;
		colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachmentInfo.resolveImageView = mSwapchainImageViews[imageIndex];
		colorAttachmentInfo.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;

		VkClearValue clearDepth{};
		clearDepth.depthStencil.depth = 1.0f;
		clearDepth.depthStencil.stencil = 0;

		VkRenderingAttachmentInfo depthAttachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthAttachmentInfo.clearValue = clearDepth;
		depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachmentInfo.imageView = mDepthView;
		depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		
		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachmentInfo;
		renderingInfo.pDepthAttachment = &depthAttachmentInfo;
		renderingInfo.renderArea = area;
		renderingInfo.layerCount = 1;

		vkCmdBeginRendering(mCommandBuffers[mFrameIndex], &renderingInfo);

		vkCmdBindPipeline(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipe);

		vkCmdBindDescriptorSets(mCommandBuffers[mFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPipeLayout, 0, 1, &mDescSet, 0, nullptr);

		VkViewport viewport{};
		viewport.width = mSwapchainExtent.width;
		viewport.height = mSwapchainExtent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		viewport.x = 0.0f;
		viewport.y = 0.0f;

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = mSwapchainExtent;

		vkCmdSetViewport(mCommandBuffers[mFrameIndex], 0, 1, &viewport);
		vkCmdSetScissor(mCommandBuffers[mFrameIndex], 0, 1, &scissor);

		static auto startTime = std::chrono::high_resolution_clock::now();
		auto currentTime = std::chrono::high_resolution_clock::now();
		auto deltaTime = std::chrono::duration<float>(currentTime - startTime).count();

		MatrixData pushConsts{};
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::rotate(model, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(2.0f * sinf(deltaTime), 0.0f, 2.0f * cosf(deltaTime)), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(75.0f), static_cast<float>(mSwapchainExtent.width) / mSwapchainExtent.height, 0.1f, 100.0f);
		proj[1][1] *= -1;
		pushConsts.mv = view * model;
		pushConsts.mvp = proj * pushConsts.mv;
		pushConsts.normal = glm::transpose(glm::inverse(glm::mat3(model)));

		vkCmdPushConstants(mCommandBuffers[mFrameIndex], mPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConsts), &pushConsts);
		
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(mCommandBuffers[mFrameIndex], 0, 1, &mVertexBuffer, offsets);

		vkCmdBindIndexBuffer(mCommandBuffers[mFrameIndex], mIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(mCommandBuffers[mFrameIndex], mIndices.size(), 1, 0, 0, 0);

		vkCmdEndRendering(mCommandBuffers[mFrameIndex]);

		TransitionSwapchainImage(
			imageIndex,
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

		VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, mRenderFences[mFrameIndex]));

		VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &mRenderSemaphores[imageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &mSwapchain;

		res = vkQueuePresentKHR(mPresentQueue, &presentInfo);
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
	}

	void App::InitInstance()
	{
		VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
		appInfo.apiVersion = VK_API_VERSION_1_4;
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pApplicationName = "VulkanApp";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "N/A";

		uint32_t wsiExtensionCount;
		auto* wsiExtensions = glfwGetRequiredInstanceExtensions(&wsiExtensionCount);
		std::vector<const char*> instanceExtensions(wsiExtensions, wsiExtensions + wsiExtensionCount);
		if constexpr (enableValidationLayers)
		{
			instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		for (const auto& extension : instanceExtensions)
		{
			if (!InstanceExtensionSupported(extension))
			{
				std::cerr << "Error: Extension " << extension << " not supported\n";
				return;
			}
		}
		
		std::vector<const char*> instanceLayers;
		if constexpr (enableValidationLayers)
		{
			instanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
			//instanceLayers.emplace_back("VK_LAYER_LUNARG_monitor");
		}

		auto debugInfo = GetDebugInfo();

		VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceInfo.pApplicationInfo = &appInfo;
		instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
		instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
		instanceInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
		instanceInfo.ppEnabledLayerNames = instanceLayers.data();
		if constexpr (enableValidationLayers)
		{
			instanceInfo.pNext = &debugInfo;
		}

		VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &mInstance));

		if constexpr (enableValidationLayers)
		{
			auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(mInstance, "vkCreateDebugUtilsMessengerEXT"));
			if (create)
			{
				VK_CHECK(create(mInstance, &debugInfo, nullptr, &mDebugMessenger));
			}
			else
			{
				std::cerr << "Failed to load vkCreateDebugUtilsMessengerEXT!\n";
			}
		}
	}

	void App::InitSurface()
	{
		VK_CHECK(glfwCreateWindowSurface(mInstance, mWindow, nullptr, &mSurface));
	}

	void App::InitDevice()
	{
		uint32_t gpuCount{};
		VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &gpuCount, nullptr));
		assert(gpuCount > 0 && "No GPUs with Vulkan detected!");
		std::vector<VkPhysicalDevice> gpus(gpuCount);
		VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &gpuCount, gpus.data()));
		for (auto gpu : gpus)
		{
			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(gpu, &props);
			if (props.apiVersion < VK_API_VERSION_1_4)
				continue;

			// Check for graphics/present queues
			uint32_t queueFamCount{};
			vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFams(queueFamCount);
			vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamCount, queueFams.data());

			std::optional<uint32_t> graphicsIndex, presentIndex;
			for (uint32_t i = 0; i < queueFamCount; ++i)
			{
				if ((queueFams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == VK_QUEUE_GRAPHICS_BIT)
					graphicsIndex = i;

				VkBool32 presentSupport;
				VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, mSurface, &presentSupport));
				if (presentSupport)
					presentIndex = i;

				if (graphicsIndex.has_value() && presentIndex.has_value())
					break;
			}

			if (!graphicsIndex.has_value() || !presentIndex.has_value())
				continue;

			std::vector<const char*> deviceExtensions =
			{
				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
			};

			bool supportsExtensions = true;
			for (auto& extension : deviceExtensions)
			{
				if (!DeviceExtensionSupported(gpu, extension))
				{
					supportsExtensions = false;
					break;
				}
			}
			
			if (!supportsExtensions)
				continue;

			// Assume graphics and present queue fam index are the same, as per the Vulkan Tutorial
			constexpr float queuePriority = 1.0f;
			VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			queueInfo.pQueuePriorities = &queuePriority;
			queueInfo.queueCount = 1;
			queueInfo.queueFamilyIndex = graphicsIndex.value();

			VkPhysicalDeviceSynchronization2Features syncFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
			syncFeatures.synchronization2 = VK_TRUE;

			VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
			dynamicRenderFeatures.pNext = &syncFeatures;
			dynamicRenderFeatures.dynamicRendering = VK_TRUE;

			VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features.pNext = &dynamicRenderFeatures;

			VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			deviceInfo.pNext = &features;
			deviceInfo.queueCreateInfoCount = 1;
			deviceInfo.pQueueCreateInfos = &queueInfo;
			deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
			deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

			VK_CHECK(vkCreateDevice(gpu, &deviceInfo, nullptr, &mDevice));

			mGpu			= gpu;
			mGraphicsIndex	= graphicsIndex.value();
			mPresentIndex	= presentIndex.value();
			vkGetDeviceQueue(mDevice, mGraphicsIndex, 0, &mGraphicsQueue);
			vkGetDeviceQueue(mDevice, mPresentIndex, 0, &mPresentQueue);

			VmaAllocatorCreateInfo allocatorInfo{};
			allocatorInfo.instance = mInstance;
			allocatorInfo.physicalDevice = mGpu;
			allocatorInfo.device = mDevice;
			VK_CHECK(vmaCreateAllocator(&allocatorInfo, &mAllocator));

			VkSampleCountFlags sampleCounts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
			if (sampleCounts & VK_SAMPLE_COUNT_16_BIT) mMsaaSamples = VK_SAMPLE_COUNT_16_BIT;
			else if (sampleCounts & VK_SAMPLE_COUNT_8_BIT) mMsaaSamples = VK_SAMPLE_COUNT_8_BIT;
			else if (sampleCounts & VK_SAMPLE_COUNT_4_BIT) mMsaaSamples = VK_SAMPLE_COUNT_4_BIT;
			else if (sampleCounts & VK_SAMPLE_COUNT_2_BIT) mMsaaSamples = VK_SAMPLE_COUNT_2_BIT;
			else mMsaaSamples = VK_SAMPLE_COUNT_1_BIT;

			return;
		}

		std::cerr << "Failed to find a suitable GPU!\n";
	}

	void App::InitSwapchain()
	{
		auto format = ChooseSurfaceFormat();
		auto presentMode = ChoosePresentMode();
		auto extent = ChooseSurfaceExtent();

		VkSurfaceCapabilitiesKHR caps{};
		VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mGpu, mSurface, &caps));
		
		VkSwapchainCreateInfoKHR swapchainInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		swapchainInfo.clipped = VK_TRUE;
		swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		swapchainInfo.imageArrayLayers = 1;
		swapchainInfo.imageFormat = format.format;
		swapchainInfo.imageColorSpace = format.colorSpace;
		swapchainInfo.presentMode = presentMode;
		swapchainInfo.imageExtent = extent;
		swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // TODO: support different graphics/present queues?
		swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		swapchainInfo.minImageCount = caps.minImageCount;
		swapchainInfo.surface = mSurface;
		swapchainInfo.preTransform = caps.currentTransform;

		VK_CHECK(vkCreateSwapchainKHR(mDevice, &swapchainInfo, nullptr, &mSwapchain));

		mSwapchainFormat = format.format;
		mSwapchainExtent = extent;

		// Get the images from the swapchain
		uint32_t swapImageCount{};
		VK_CHECK(vkGetSwapchainImagesKHR(mDevice, mSwapchain, &swapImageCount, nullptr));
		mSwapchainImages.resize(swapImageCount);
		VK_CHECK(vkGetSwapchainImagesKHR(mDevice, mSwapchain, &swapImageCount, mSwapchainImages.data()));

		mSwapchainImageViews.reserve(swapImageCount);
		for (const auto& image : mSwapchainImages)
		{
			VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.image = image;
			viewInfo.format = mSwapchainFormat;
			viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;

			VkImageView view{};
			VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &view));
			mSwapchainImageViews.emplace_back(view);
		}
	}

	void App::InitPipeline()
	{
		auto shaderSource = ReadFile("Assets/Shaders/Basic.spv");
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
		inputAttribs[0].binding = 0;
		inputAttribs[0].location = 0;
		inputAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		inputAttribs[0].offset = 0;
		inputAttribs[1].binding = 0;
		inputAttribs[1].location = 1;
		inputAttribs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		inputAttribs[1].offset = sizeof(float) * 3;
		inputAttribs[2].binding = 0;
		inputAttribs[2].location = 2;
		inputAttribs[2].format = VK_FORMAT_R32G32_SFLOAT;
		inputAttribs[2].offset = sizeof(float) * 7;
		inputAttribs[3].binding = 0;
		inputAttribs[3].location = 3;
		inputAttribs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
		inputAttribs[3].offset = sizeof(float) * 9;

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
		multisample.rasterizationSamples = mMsaaSamples;

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

		VkDescriptorSetLayoutBinding imageBinding{};
		imageBinding.binding = 0;
		imageBinding.descriptorCount = 1;
		imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		imageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.bindingCount = 1;
		setLayoutInfo.pBindings = &imageBinding;

		VK_CHECK(vkCreateDescriptorSetLayout(mDevice, &setLayoutInfo, nullptr, &mSetLayout));

		VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &matrixRange;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &mSetLayout;
		
		VK_CHECK(vkCreatePipelineLayout(mDevice, &layoutInfo, nullptr, &mPipeLayout));

		mDepthFormat = GetSupportedFormat
		(
			{
				VK_FORMAT_D32_SFLOAT,
				VK_FORMAT_D32_SFLOAT_S8_UINT,
				VK_FORMAT_D24_UNORM_S8_UINT
			},
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);

		VkPipelineRenderingCreateInfo renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &mSwapchainFormat;
		renderingInfo.depthAttachmentFormat = mDepthFormat;

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

		VK_CHECK(vkCreateGraphicsPipelines(mDevice, nullptr, 1, &pipelineInfo, nullptr, &mPipe));

		vkDestroyShaderModule(mDevice, shader, nullptr);
	}

	void App::InitCommandPool()
	{
		VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		poolInfo.queueFamilyIndex = mGraphicsIndex;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		
		VK_CHECK(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool));

		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		VK_CHECK(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mTransientPool));
	}

	void App::InitCommandBuffers()
	{
		mCommandBuffers.clear();
		mCommandBuffers.resize(MaxFramesInFlight);

		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandBufferCount = MaxFramesInFlight;
		allocInfo.commandPool = mCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		
		VK_CHECK(vkAllocateCommandBuffers(mDevice, &allocInfo, mCommandBuffers.data()));
	}

	void App::InitMsaaTarget()
	{
		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.arrayLayers = 1;
		imageInfo.extent = { mSwapchainExtent.width, mSwapchainExtent.height, 1 };
		imageInfo.format = mSwapchainFormat;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.mipLevels = 1;
		imageInfo.samples = mMsaaSamples;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VK_CHECK(vmaCreateImage(mAllocator, &imageInfo, &allocInfo, &mMsaaImage, &mMsaaAllocation, nullptr));

		auto cmds = CreateImmediateCommandBuffer();

		VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.image = mMsaaImage;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		imageBarrier.srcAccessMask = VK_ACCESS_2_NONE;
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(cmds, &depInfo);

		SubmitImmediateCommandBuffer(cmds);

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.format = imageInfo.format;
		viewInfo.image = mMsaaImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;

		VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mMsaaView));
	}

	void App::InitDepthBuffer()
	{
		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.arrayLayers = 1;
		imageInfo.extent = { mSwapchainExtent.width, mSwapchainExtent.height, 1 };
		imageInfo.format = mDepthFormat;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.mipLevels = 1;
		imageInfo.samples = mMsaaSamples;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VK_CHECK(vmaCreateImage(mAllocator, &imageInfo, &allocInfo, &mDepthImage, &mDepthAllocation, nullptr));

		auto cmds = CreateImmediateCommandBuffer();

		VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.image = mDepthImage;
		imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
		imageBarrier.srcAccessMask = VK_ACCESS_2_NONE;
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | (HasStencilComponent(mDepthFormat) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(cmds, &depInfo);

		SubmitImmediateCommandBuffer(cmds);

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.format = imageInfo.format;
		viewInfo.image = mDepthImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;

		VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mDepthView));
	}

	void App::InitDescriptorPool()
	{
		VkDescriptorPoolSize sizes{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };

		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &sizes;

		VK_CHECK(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &mDescPool));
	}

	void App::InitSyncPrimitives()
	{
		mAcquireSemaphores.clear();
		mRenderSemaphores.clear();
		mRenderFences.clear();

		mAcquireSemaphores.resize(MaxFramesInFlight);
		mRenderSemaphores.resize(MaxFramesInFlight);
		mRenderFences.resize(MaxFramesInFlight);

		VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (size_t i = 0; i < mSwapchainImages.size(); ++i)
		{
			VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &mAcquireSemaphores[i]));
			VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &mRenderSemaphores[i]));
		}

		for (size_t i = 0; i < MaxFramesInFlight; ++i)
		{
			VK_CHECK(vkCreateFence(mDevice, &fenceInfo, nullptr, &mRenderFences[i]));
		}
	}

	void App::InitModel()
	{
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn;
		std::string error;
		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &error, "Assets/Models/viking_room.obj"))
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
					attrib.vertices[3 * index.normal_index + 0],
					attrib.vertices[3 * index.normal_index + 1],
					attrib.vertices[3 * index.normal_index + 2]);

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
		
		auto [stagingBuffer, stagingAllocation] = CreateBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			size,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

		void* data;
		VK_CHECK(vmaMapMemory(mAllocator, stagingAllocation, &data));
		std::memcpy(data, mVertices.data(), size);
		vmaUnmapMemory(mAllocator, stagingAllocation);

		std::tie(mVertexBuffer, mVertexBufferAllocation) = CreateBuffer(
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			size,
			0);

		auto cmds = CreateImmediateCommandBuffer();
		CopyBuffer(cmds, stagingBuffer, mVertexBuffer, size);
		SubmitImmediateCommandBuffer(cmds);

		vmaDestroyBuffer(mAllocator, stagingBuffer, stagingAllocation);
	}

	void App::InitIndexBuffer()
	{
		const auto size = mIndices.size() * sizeof(mIndices[0]);

		auto [stagingBuffer, stagingAllocation] = CreateBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			size,
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

		void* data;
		VK_CHECK(vmaMapMemory(mAllocator, stagingAllocation, &data));
		std::memcpy(data, mIndices.data(), size);
		vmaUnmapMemory(mAllocator, stagingAllocation);

		std::tie(mIndexBuffer, mIndexBufferAllocation) = CreateBuffer(
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			size,
			0);

		auto cmds = CreateImmediateCommandBuffer();
		CopyBuffer(cmds, stagingBuffer, mIndexBuffer, size);
		SubmitImmediateCommandBuffer(cmds);

		vmaDestroyBuffer(mAllocator, stagingBuffer, stagingAllocation);
	}

	void App::InitTexture()
	{
		stbi_set_flip_vertically_on_load(true);

		constexpr VkDeviceSize bytesPerPixel = 4;
		int width, height, channels;
		stbi_uc* data = stbi_load("Assets/Textures/viking_room.png", &width, &height, &channels, STBI_rgb_alpha);
		if (!data)
		{
			std::cerr << "Failed to load texture image!\n";
		}
		
		mMipLevelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

		const VkDeviceSize size = width * height * bytesPerPixel;

		auto [staging, stagingAllocation] = CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		void* mappedData;
		VK_CHECK(vmaMapMemory(mAllocator, stagingAllocation, &mappedData));
		std::memcpy(mappedData, data, size);
		vmaUnmapMemory(mAllocator, stagingAllocation);

		stbi_image_free(data);

		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.arrayLayers = 1;
		imageInfo.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
		imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.mipLevels = mMipLevelCount;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VK_CHECK(vmaCreateImage(mAllocator, &imageInfo, &allocInfo, &mTexture, &mTextureAllocation, nullptr));

		auto cmds = CreateImmediateCommandBuffer();

		{
			VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
			imageBarrier.image = mTexture;
			imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
			imageBarrier.srcAccessMask = VK_ACCESS_2_NONE;
			imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageBarrier.subresourceRange.baseArrayLayer = 0;
			imageBarrier.subresourceRange.layerCount = 1;
			imageBarrier.subresourceRange.baseMipLevel = 0;
			imageBarrier.subresourceRange.levelCount = mMipLevelCount;

			VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			depInfo.imageMemoryBarrierCount = 1;
			depInfo.pImageMemoryBarriers = &imageBarrier;

			vkCmdPipelineBarrier2(cmds, &depInfo);
		}

		VkBufferImageCopy buffer2Image{};
		buffer2Image.imageExtent = imageInfo.extent;
		buffer2Image.imageOffset = { 0, 0, 0 };
		buffer2Image.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		buffer2Image.imageSubresource.baseArrayLayer = 0;
		buffer2Image.imageSubresource.layerCount = 1;
		buffer2Image.imageSubresource.mipLevel = 0;
		buffer2Image.bufferImageHeight = 0;
		buffer2Image.bufferOffset = 0;
		buffer2Image.bufferRowLength = 0;
		vkCmdCopyBufferToImage(cmds, staging, mTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer2Image);

		SubmitImmediateCommandBuffer(cmds);

		vmaDestroyBuffer(mAllocator, staging, stagingAllocation);

		GenerateMipmaps(mTexture, imageInfo.format, width, height, mMipLevelCount);

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.format = imageInfo.format;
		viewInfo.image = mTexture;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mMipLevelCount;

		VK_CHECK(vkCreateImageView(mDevice, &viewInfo, nullptr, &mTextureView));

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

		VK_CHECK(vkCreateSampler(mDevice, &samplerInfo, nullptr, &mTextureSampler));
	}

	void App::InitDescriptorSets()
	{
		VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocInfo.descriptorPool = mDescPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &mSetLayout;

		VK_CHECK(vkAllocateDescriptorSets(mDevice, &allocInfo, &mDescSet));

		VkDescriptorImageInfo imageWrite{};
		imageWrite.imageView = mTextureView;
		imageWrite.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageWrite.sampler = mTextureSampler;

		VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.dstArrayElement = 0;
		write.dstBinding = 0;
		write.dstSet = mDescSet;
		write.pImageInfo = &imageWrite;

		vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
	}

	void App::CleanupSwapchain()
	{
		vkDestroyImageView(mDevice, mDepthView, nullptr);
		vmaDestroyImage(mAllocator, mDepthImage, mDepthAllocation);

		vkDestroyImageView(mDevice, mMsaaView, nullptr);
		vmaDestroyImage(mAllocator, mMsaaImage, mMsaaAllocation);

		for (const auto& view : mSwapchainImageViews)
			vkDestroyImageView(mDevice, view, nullptr);

		vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);

		mSwapchainImageViews.clear();
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

		VK_CHECK(vkDeviceWaitIdle(mDevice));

		CleanupSwapchain();
		InitSwapchain();
		InitMsaaTarget();
		InitDepthBuffer();
	}

	VkSurfaceFormatKHR App::ChooseSurfaceFormat()
	{
		uint32_t formatCount{};
		VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(mGpu, mSurface, &formatCount, nullptr));
		assert(formatCount > 0 && "GPU has no surface formats available");
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(mGpu, mSurface, &formatCount, formats.data()));
		auto it = std::find_if(
			formats.begin(),
			formats.end(),
			[](const VkSurfaceFormatKHR& format) { return format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; }
		);

		if (it != formats.end())
		{
			return *it;
		}
		else
		{
			return formats[0];
		}
	}

	VkPresentModeKHR App::ChoosePresentMode()
	{
		uint32_t modeCount{};
		VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(mGpu, mSurface, &modeCount, nullptr));
		assert(modeCount > 0 && "GPU has no present modes available");
		std::vector<VkPresentModeKHR> modes(modeCount);
		VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(mGpu, mSurface, &modeCount, nullptr));
		auto it = std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR);
		if (it != modes.end())
			return *it;
		else
			return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D App::ChooseSurfaceExtent()
	{
		VkSurfaceCapabilitiesKHR caps{};
		VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mGpu, mSurface, &caps));
		if (caps.currentExtent.width != UINT32_MAX && caps.currentExtent.height != UINT32_MAX)
		{
			return caps.currentExtent;
		}
		else
		{
			VkExtent2D res{};
			glfwGetFramebufferSize(mWindow, reinterpret_cast<int*>(res.width), reinterpret_cast<int*>(res.height));
			res.width = std::clamp(res.width, caps.minImageExtent.width, caps.maxImageExtent.width);
			res.height = std::clamp(res.height, caps.minImageExtent.height, caps.maxImageExtent.height);
			return res;
		}
	}

	VkShaderModule App::CreateShader(const std::vector<char>& source)
	{
		VkShaderModuleCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		shaderInfo.codeSize = source.size() * sizeof(source[0]);
		shaderInfo.pCode = reinterpret_cast<const uint32_t*>(source.data());

		VkShaderModule res;
		VK_CHECK(vkCreateShaderModule(mDevice, &shaderInfo, nullptr, &res));
		return res;
	}

	std::pair<VkBuffer, VmaAllocation> App::CreateBuffer(VkBufferUsageFlags usage, VkDeviceSize size, VmaAllocationCreateFlags vmaFlags)
	{
		VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = vmaFlags;

		std::pair<VkBuffer, VmaAllocation> res;
		VK_CHECK(vmaCreateBuffer(mAllocator, &bufferInfo, &allocInfo, &res.first, &res.second, nullptr));
		return res;
	}

	void App::TransitionSwapchainImage(
		uint32_t imageIndex,
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
		barrier.image = mSwapchainImages[imageIndex];
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

	VkFormat App::GetSupportedFormat(const std::initializer_list<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags flags)
	{
		for (auto fmt : formats)
		{
			VkFormatProperties props{};
			vkGetPhysicalDeviceFormatProperties(mGpu, fmt, &props);
			if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & flags) == flags)
				return fmt;

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & flags) == flags)
				return fmt;
		}

		std::cerr << "Failed to find supported format!\n";
	}

	VkCommandBuffer App::CreateImmediateCommandBuffer()
	{
		VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = mTransientPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		VkCommandBuffer cmds;
		VK_CHECK(vkAllocateCommandBuffers(mDevice, &allocInfo, &cmds));

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

		VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
		VK_CHECK(vkQueueWaitIdle(mGraphicsQueue));

		vkFreeCommandBuffers(mDevice, mTransientPool, 1, &commandBuffer);
	}

	void App::GenerateMipmaps(VkImage image, VkFormat format, int width, int height, uint32_t levelCount)
	{
		VkFormatProperties props{};
		vkGetPhysicalDeviceFormatProperties(mGpu, format, &props);
		if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		{
			std::cerr << "Image does not support linear blit!\n";
		}

		auto cmds = CreateImmediateCommandBuffer();

		VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &barrier;

		int currentWidth = width;
		int currentHeight = height;

		for (int i = 1; i < levelCount; ++i)
		{
			barrier.subresourceRange.baseMipLevel = i - 1;

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier2(cmds, &depInfo);

			VkImageBlit blit{};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { currentWidth, currentHeight, 1 };
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { currentWidth > 1 ? currentWidth / 2 : 1, currentHeight > 1 ? currentHeight / 2 : 1, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.srcSubresource.mipLevel = i - 1;
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;
			blit.dstSubresource.mipLevel = i;

			vkCmdBlitImage(cmds, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
			
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

			vkCmdPipelineBarrier2(cmds, &depInfo);

			if (currentWidth > 1) currentWidth /= 2;
			if (currentHeight > 1) currentHeight /= 2;
		}

		barrier.subresourceRange.baseMipLevel = levelCount - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

		vkCmdPipelineBarrier2(cmds, &depInfo);

		SubmitImmediateCommandBuffer(cmds);
	}

	void App::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
	{
		App* app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
		app->mFramebufferResized = true;
	}

	bool App::InstanceExtensionSupported(const char* name)
	{
		uint32_t extensionCount{};
		VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr));
		std::vector<VkExtensionProperties> extensions(extensionCount);
		VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()));

		auto it = std::find_if(
			extensions.begin(),
			extensions.end(),
			[name](const VkExtensionProperties& ext)
			{
				return std::strcmp(name, ext.extensionName) == 0;
			}
		);

		return it != extensions.end();
	}

	bool App::DeviceExtensionSupported(VkPhysicalDevice gpu, const char* name)
	{
		uint32_t extensionCount{};
		VK_CHECK(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, nullptr));
		std::vector<VkExtensionProperties> extensions(extensionCount);
		VK_CHECK(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, extensions.data()));

		auto it = std::find_if(
			extensions.begin(),
			extensions.end(),
			[name](const VkExtensionProperties& ext)
			{
				return std::strcmp(name, ext.extensionName) == 0;
			}
		);

		return it != extensions.end();
	}

	VkDebugUtilsMessengerCreateInfoEXT App::GetDebugInfo()
	{
		VkDebugUtilsMessengerCreateInfoEXT debugInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
		debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.pfnUserCallback = &DebugMessengerCallback;
		return debugInfo;
	}

	std::vector<char> App::ReadFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			std::cerr << "Error: failed to open file with path '" << path << "\'\n";
			return {};
		}

		auto size = file.tellg();
		std::vector<char> res(size);
		file.seekg(0, std::ios::beg);
		file.read(res.data(), size);

		return res;
	}

	VkPipelineShaderStageCreateInfo App::MakeShaderStage(VkShaderModule shader, VkShaderStageFlagBits stage, const char* entrypoint)
	{
		VkPipelineShaderStageCreateInfo res{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		res.module = shader;
		res.pName = entrypoint;
		res.stage = stage;
		return res;
	}

	bool App::HasStencilComponent(VkFormat format)
	{
		return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL App::DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* data,
		void* userData)
	{
		switch (severity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			std::cout << "[Vulkan] Debug: " << data->pMessage << '\n';
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			std::cout << "[Vulkan] Info: " << data->pMessage << '\n';
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			std::cerr << "[Vulkan] Warning: " << data->pMessage << '\n';
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			std::cerr << "[Vulkan] Error: " << data->pMessage << '\n';
			break;
		}
		return VK_FALSE;
	}
}