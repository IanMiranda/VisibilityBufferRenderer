#pragma once

#include "Common.h"

namespace im
{
	class Device;
	class Semaphore;

	class Swapchain
	{
	public:
		Swapchain(Device& device);
		~Swapchain();

		Swapchain(const Swapchain& other) = delete;
		Swapchain& operator=(const Swapchain& other) = delete;

		VkFormat GetFormat() const { return mSwapchainFormat; }
		VkExtent2D GetExtent() const { return mSwapchainExtent; }
		const std::vector<VkImage>& GetImages() const { return mSwapchainImages; }
		const std::vector<VkImageView>& GetViews() const { return mSwapchainImageViews; }
		uint32_t GetImageIndex() const { return mLastImageIndex; }

		std::pair<VkResult, uint32_t> AcquireNextImage(const Semaphore& acquiredSemaphore);
		VkResult Present(const Semaphore& presentedSemaphore);

		void Recreate();

	private:
		void Init();

		void CleanUp();

		VkSurfaceFormatKHR ChooseSurfaceFormat();
		VkPresentModeKHR ChoosePresentMode();
		VkExtent2D ChooseSurfaceExtent();

	private:
		Device& mDevice;

		VkSwapchainKHR mSwapchain{ VK_NULL_HANDLE };
		std::vector<VkImage> mSwapchainImages;
		std::vector<VkImageView> mSwapchainImageViews;

		VkFormat mSwapchainFormat;
		VkExtent2D mSwapchainExtent;

		uint32_t mLastImageIndex;
	};
}