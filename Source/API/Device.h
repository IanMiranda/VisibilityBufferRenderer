#pragma once

#include "Common.h"
#include "Swapchain.h"
#include "Sampler.h"

namespace im
{
	class CommandBuffer;
	class CommandPool;
	class Fence;

	class Device
	{
	public:
		Device(GLFWwindow* window);
		~Device();

		Device(const Device& other) = delete;
		Device& operator=(const Device& other) = delete;

		GLFWwindow* GetWindow() const { return mWindow; }
		VkInstance GetInstance() const { return mInstance; }
		VkSurfaceKHR GetSurface() const { return mSurface; }
		VkPhysicalDevice GetGpu() const { return mGpu; }
		VkDevice Get() const { return mDevice; }
		VmaAllocator GetAllocator() const { return mAllocator; }
		VkPipelineCache GetPipelineCache() const { return mPipelineCache; }
		
		Swapchain& GetSwapchain() { return *mSwapchain; }
		Samplers& GetSamplers() { return *mSamplers; }

		uint32_t GetGraphicsIndex() const { return mGraphicsIndex; }
		VkQueue GetGraphicsQueue() const { return mGraphicsQueue; }
		uint32_t GetPresentIndex() const { return mPresentIndex; }
		VkQueue GetPresentQueue() const { return mPresentQueue; }

		void Submit(CommandBuffer& cmd,
			Semaphore* waitSemaphore = nullptr,
			VkPipelineStageFlags waitDstStage = VK_PIPELINE_STAGE_2_NONE,
			Semaphore* signalSemaphore = nullptr,
			Fence* fence = nullptr);
		void SubmitAndFlush(CommandBuffer& cmd);
		void WaitIdle();

		VkFormat GetSupportedFormat(const std::initializer_list<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags flags) const;
		VkFormat GetDepthFormat() const;

		void RunImmediateCommands(const std::function<void(CommandBuffer&)>& cmds);

	private:
		void InitInstance();
		void InitSurface();
		void InitDevice();
		void InitPipelineCache();

	private:
		static bool InstanceExtensionSupported(const char* name);
		static bool DeviceExtensionSupported(VkPhysicalDevice gpu, const char* name);

		static VkDebugUtilsMessengerCreateInfoEXT GetDebugInfo();

		static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT type,
			const VkDebugUtilsMessengerCallbackDataEXT* data,
			void* userData);

	private:
		GLFWwindow* mWindow;

		VkInstance mInstance{ VK_NULL_HANDLE };
		VkDebugUtilsMessengerEXT mDebugMessenger{ VK_NULL_HANDLE };
		VkSurfaceKHR mSurface{ VK_NULL_HANDLE };
		VkPhysicalDevice mGpu{ VK_NULL_HANDLE };
		VkDevice mDevice{ VK_NULL_HANDLE };
		VmaAllocator mAllocator{ VK_NULL_HANDLE };
		VkPipelineCache mPipelineCache{ VK_NULL_HANDLE };

		VkQueue mGraphicsQueue{ VK_NULL_HANDLE };
		VkQueue mPresentQueue{ VK_NULL_HANDLE };

		uint32_t mGraphicsIndex;
		uint32_t mPresentIndex;

		std::unique_ptr<Swapchain> mSwapchain;
		std::unique_ptr<CommandPool> mImmediatePool;
		std::unique_ptr<Samplers> mSamplers;
	};
}