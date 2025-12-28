#include "Device.h"

#include <optional>
#include <cassert>

#include "CommandBuffer.h"
#include "CommandPool.h"
#include "Fence.h"
#include "Semaphore.h"

namespace im
{
#ifndef NDEBUG
	constexpr bool gEnableValidationLayers = true;
#else
	constexpr bool gEnableValidationLayers = false;
#endif

	Device::Device(GLFWwindow* window) : mWindow(window)
	{
		InitInstance();
		InitSurface();
		InitDevice();
		InitPipelineCache();

		mSwapchain = std::make_unique<Swapchain>(*this);
		mImmediatePool = std::make_unique<CommandPool>(*this, GetGraphicsIndex(), VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
		mSamplers = std::make_unique<Samplers>(*this);
	}

	Device::~Device()
	{
		WaitIdle();

		mSamplers.reset();
		mImmediatePool.reset();
		mSwapchain.reset();

		vkDestroyPipelineCache(mDevice, mPipelineCache, nullptr);
		vmaDestroyAllocator(mAllocator);
		vkDestroyDevice(mDevice, nullptr);
		vkDestroySurfaceKHR(mInstance, mSurface, nullptr);

		if constexpr (gEnableValidationLayers)
		{
			if (const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
					vkGetInstanceProcAddr(mInstance, "vkDestroyDebugUtilsMessengerEXT")
				);
				destroy)
				destroy(mInstance, mDebugMessenger, nullptr);
			else
				fmt::println(stderr, "Failed to load vkDestroyDebugUtilsMessengerEXT!");
		}

		vkDestroyInstance(mInstance, nullptr);
	}

	void Device::Submit(CommandBuffer& cmd, Semaphore* waitSemaphore, VkPipelineStageFlags waitDstStage, Semaphore* signalSemaphore, Fence* fence)
	{
		const std::array cmds{ cmd.Get() };
		const std::array waitSems{ waitSemaphore ? waitSemaphore->Get() : nullptr };
		const std::array signalSems{ signalSemaphore ? signalSemaphore->Get() : nullptr };

		VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
		submitInfo.commandBufferCount = cmds.size();
		submitInfo.pCommandBuffers = cmds.data();
		submitInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
		submitInfo.pWaitSemaphores = waitSems.data();
		submitInfo.pWaitDstStageMask = &waitDstStage;
		submitInfo.signalSemaphoreCount = signalSemaphore ? 1 : 0;
		submitInfo.pSignalSemaphores = signalSems.data();

		VK_CHECK(vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, (fence ? fence->Get() : nullptr)));
	}

	void Device::SubmitAndFlush(CommandBuffer& cmd)
	{
		Submit(cmd);
		VK_CHECK(vkQueueWaitIdle(mGraphicsQueue));
	}

	void Device::WaitIdle()
	{
		VK_CHECK(vkDeviceWaitIdle(mDevice));
	}

    VkFormat Device::GetSupportedFormat(const std::initializer_list<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags flags) const
	{
		for (const auto fmt : formats)
		{
			VkFormatProperties props{};
			vkGetPhysicalDeviceFormatProperties(mGpu, fmt, &props);
			if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & flags) == flags)
				return fmt;

			if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & flags) == flags)
				return fmt;
		}

		fmt::println(stderr, "Failed to find supported format!");
		return VK_FORMAT_UNDEFINED;
	}

	VkFormat Device::GetDepthFormat() const
	{
		return GetSupportedFormat
		(
			{
				VK_FORMAT_D32_SFLOAT,
				VK_FORMAT_D32_SFLOAT_S8_UINT,
				VK_FORMAT_D24_UNORM_S8_UINT
			},
			VK_IMAGE_TILING_OPTIMAL,
			VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		);
	}

	void Device::RunImmediateCommands(const std::function<void(CommandBuffer&)>& cmds)
	{
		auto cmdBuf = mImmediatePool->Allocate();
		cmdBuf->Begin();
		cmds(*cmdBuf);
		cmdBuf->End();
		SubmitAndFlush(*cmdBuf);
	}

	void Device::InitInstance()
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
		if constexpr (gEnableValidationLayers)
		{
			instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		for (const auto& extension : instanceExtensions)
		{
			if (!InstanceExtensionSupported(extension))
			{
				fmt::println(stderr, "Error: Extension {} not supported", extension);
				return;
			}
		}

		if (InstanceExtensionSupported(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		{
			instanceExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		}

		std::vector<const char*> instanceLayers;
		if constexpr (gEnableValidationLayers)
		{
			instanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
			// instanceLayers.emplace_back("VK_LAYER_LUNARG_monitor");
		}

		const auto debugInfo = GetDebugInfo();

		VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		if (InstanceExtensionSupported(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
		{
			instanceExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
			instanceInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}
		instanceInfo.pApplicationInfo = &appInfo;
		instanceInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
		instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
		instanceInfo.enabledLayerCount = static_cast<uint32_t>(instanceLayers.size());
		instanceInfo.ppEnabledLayerNames = instanceLayers.data();
		if constexpr (gEnableValidationLayers)
		{
			instanceInfo.pNext = &debugInfo;
		}

		VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &mInstance));

		if constexpr (gEnableValidationLayers)
		{
			if (const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(mInstance, "vkCreateDebugUtilsMessengerEXT"));
				create)
			{
				VK_CHECK(create(mInstance, &debugInfo, nullptr, &mDebugMessenger));
			}
			else
			{
				fmt::println(stderr, "Failed to load vkCreateDebugUtilsMessengerEXT!");
			}
		}
	}

	void Device::InitSurface()
	{
		VK_CHECK(glfwCreateWindowSurface(mInstance, mWindow, nullptr, &mSurface));
	}

	void Device::InitDevice()
	{
		uint32_t gpuCount{};
		VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &gpuCount, nullptr));
		assert(gpuCount > 0 && "No GPUs with Vulkan detected!");
		std::vector<VkPhysicalDevice> gpus(gpuCount);
		VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &gpuCount, gpus.data()));
		for (const auto gpu : gpus)
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
				VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
				VK_KHR_MAINTENANCE1_EXTENSION_NAME,
				VK_EXT_MESH_SHADER_EXTENSION_NAME
			};

			bool supportsExtensions = true;
			for (const auto& extension : deviceExtensions)
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

			VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
			meshShaderFeatures.meshShader = VK_TRUE;
			meshShaderFeatures.taskShader = VK_TRUE;

			VkPhysicalDeviceVulkan11Features vulkan11Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
			vulkan11Features.pNext = &meshShaderFeatures;
			vulkan11Features.shaderDrawParameters = VK_TRUE;

			VkPhysicalDeviceVulkan12Features vulkan12Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			vulkan12Features.pNext = &vulkan11Features;
			vulkan12Features.descriptorIndexing								= VK_TRUE;
			vulkan12Features.descriptorBindingPartiallyBound				= VK_TRUE;
			vulkan12Features.descriptorBindingSampledImageUpdateAfterBind	= VK_TRUE;
			vulkan12Features.descriptorBindingUpdateUnusedWhilePending		= VK_TRUE;
			vulkan12Features.descriptorBindingVariableDescriptorCount		= VK_TRUE;
			vulkan12Features.shaderSampledImageArrayNonUniformIndexing		= VK_TRUE;
			vulkan12Features.runtimeDescriptorArray							= VK_TRUE;
			vulkan12Features.bufferDeviceAddress							= VK_TRUE;
			vulkan12Features.scalarBlockLayout								= VK_TRUE;
			vulkan12Features.shaderInt8										= VK_TRUE;
			vulkan12Features.uniformAndStorageBuffer8BitAccess				= VK_TRUE;
			vulkan12Features.storagePushConstant8							= VK_TRUE;

			VkPhysicalDeviceSynchronization2Features syncFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
			syncFeatures.pNext = &vulkan12Features;
			syncFeatures.synchronization2 = VK_TRUE;

			VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
			dynamicRenderFeatures.pNext = &syncFeatures;
			dynamicRenderFeatures.dynamicRendering = VK_TRUE;

			VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features.pNext = &dynamicRenderFeatures;
			features.features.multiDrawIndirect = VK_TRUE;

			if (InstanceExtensionSupported("VK_KHR_portability_subset"))
			{
				deviceExtensions.emplace_back("VK_KHR_portability_subset");
			}

			VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			deviceInfo.pNext = &features;
			deviceInfo.queueCreateInfoCount = 1;
			deviceInfo.pQueueCreateInfos = &queueInfo;
			deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
			deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

			VK_CHECK(vkCreateDevice(gpu, &deviceInfo, nullptr, &mDevice));

			mGpu = gpu;
			mGraphicsIndex = graphicsIndex.value();
			mPresentIndex = presentIndex.value();
			vkGetDeviceQueue(mDevice, mGraphicsIndex, 0, &mGraphicsQueue);
			vkGetDeviceQueue(mDevice, mPresentIndex, 0, &mPresentQueue);

			VmaAllocatorCreateInfo allocatorInfo{};
			allocatorInfo.instance = mInstance;
			allocatorInfo.physicalDevice = mGpu;
			allocatorInfo.device = mDevice;
			allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
			VK_CHECK(vmaCreateAllocator(&allocatorInfo, &mAllocator));

			return;
		}

		fmt::println(stderr, "Failed to find a suitable GPU!");
	}

	void Device::InitPipelineCache()
	{
		VkPipelineCacheCreateInfo cacheInfo{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		VK_CHECK(vkCreatePipelineCache(mDevice, &cacheInfo, nullptr, &mPipelineCache));
	}

	bool Device::InstanceExtensionSupported(const char* name)
	{
		uint32_t extensionCount{};
		VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr));
		std::vector<VkExtensionProperties> extensions(extensionCount);
		VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()));

		const auto it = std::find_if(
			extensions.cbegin(),
			extensions.cend(),
			[name](const VkExtensionProperties& ext)
			{
				return std::strcmp(name, ext.extensionName) == 0;
			}
		);

		return it != extensions.end();
	}

	bool Device::DeviceExtensionSupported(VkPhysicalDevice gpu, const char* name)
	{
		uint32_t extensionCount{};
		VK_CHECK(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, nullptr));
		std::vector<VkExtensionProperties> extensions(extensionCount);
		VK_CHECK(vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extensionCount, extensions.data()));

		const auto it = std::find_if(
			extensions.cbegin(),
			extensions.cend(),
			[name](const VkExtensionProperties& ext)
			{
				return std::strcmp(name, ext.extensionName) == 0;
			}
		);

		return it != extensions.end();
	}

	VkDebugUtilsMessengerCreateInfoEXT Device::GetDebugInfo()
	{
		VkDebugUtilsMessengerCreateInfoEXT debugInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
		debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
		debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		debugInfo.pfnUserCallback = &DebugMessengerCallback;
		return debugInfo;
	}

	VKAPI_ATTR VkBool32 VKAPI_CALL Device::DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* data,
		void* userData)
	{
		switch (severity)
		{
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			fmt::println("[Vulkan] Debug: {}", data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			fmt::println("[Vulkan] Info: {}", data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			fmt::println(stderr, "[Vulkan] Warning: {}", data->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			fmt::println(stderr, "[Vulkan] Error: {}", data->pMessage);
			break;
		default:
			break;
		}
		return VK_FALSE;
	}
}