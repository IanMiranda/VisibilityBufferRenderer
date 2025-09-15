#include "Device.h"

#include <optional>
#include <cassert>

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

		mSwapchain = std::make_unique<Swapchain>(*this);
	}

	Device::~Device()
	{
		WaitIdle();

		mSwapchain.reset();

		vmaDestroyAllocator(mAllocator);
		vkDestroyDevice(mDevice, nullptr);
		vkDestroySurfaceKHR(mInstance, mSurface, nullptr);

		if constexpr (gEnableValidationLayers)
		{
			auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(mInstance, "vkDestroyDebugUtilsMessengerEXT"));

			if (destroy)
				destroy(mInstance, mDebugMessenger, nullptr);
			else
				std::cerr << "Failed to load vkDestroyDebugUtilsMessengerEXT!\n";
		}

		vkDestroyInstance(mInstance, nullptr);
	}

	void Device::WaitIdle()
	{
		VK_CHECK(vkDeviceWaitIdle(mDevice));
	}

	VkFormat Device::GetSupportedFormat(const std::initializer_list<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags flags)
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
		return VK_FORMAT_UNDEFINED;
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
				std::cerr << "Error: Extension " << extension << " not supported\n";
				return;
			}
		}

		std::vector<const char*> instanceLayers;
		if constexpr (gEnableValidationLayers)
		{
			instanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
			instanceLayers.emplace_back("VK_LAYER_LUNARG_monitor");
		}

		auto debugInfo = GetDebugInfo();

		VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
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
				VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
				VK_KHR_MAINTENANCE1_EXTENSION_NAME,
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

			VkPhysicalDeviceScalarBlockLayoutFeatures scalarBlockFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES };
			scalarBlockFeatures.scalarBlockLayout = VK_TRUE;

			VkPhysicalDeviceSynchronization2Features syncFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
			syncFeatures.pNext = &scalarBlockFeatures;
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

			mGpu = gpu;
			mGraphicsIndex = graphicsIndex.value();
			mPresentIndex = presentIndex.value();
			vkGetDeviceQueue(mDevice, mGraphicsIndex, 0, &mGraphicsQueue);
			vkGetDeviceQueue(mDevice, mPresentIndex, 0, &mPresentQueue);

			VmaAllocatorCreateInfo allocatorInfo{};
			allocatorInfo.instance = mInstance;
			allocatorInfo.physicalDevice = mGpu;
			allocatorInfo.device = mDevice;
			VK_CHECK(vmaCreateAllocator(&allocatorInfo, &mAllocator));

			return;
		}

		std::cerr << "Failed to find a suitable GPU!\n";
	}

	bool Device::InstanceExtensionSupported(const char* name)
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

	bool Device::DeviceExtensionSupported(VkPhysicalDevice gpu, const char* name)
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