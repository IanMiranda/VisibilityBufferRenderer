#include "Swapchain.h"

#include <algorithm>
#include <cassert>

#include "Device.h"
#include "Semaphore.h"

namespace im
{
	Swapchain::Swapchain(Device& device) : mDevice(device)
	{
		Init();
	}

	Swapchain::~Swapchain()
	{
		mDevice.WaitIdle();
		CleanUp();
	}

	std::pair<VkResult, uint32_t> Swapchain::AcquireNextImage(const Semaphore& acquiredSemaphore)
	{
		VkResult res = vkAcquireNextImageKHR(mDevice.Get(), mSwapchain, UINT64_MAX, acquiredSemaphore.Get(), nullptr, &mLastImageIndex);
		return { res, mLastImageIndex };
	}

	VkResult Swapchain::Present(const Semaphore& presentedSemaphore)
	{
		const VkSemaphore ps = presentedSemaphore.Get();
		VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.pImageIndices = &mLastImageIndex;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &ps;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &mSwapchain;

		return vkQueuePresentKHR(mDevice.GetPresentQueue(), &presentInfo);
	}

	void Swapchain::Recreate()
	{
		CleanUp();
		Init();
	}

	void Swapchain::Init()
	{
		const auto dev = mDevice.Get();
		const auto surf = mDevice.GetSurface();

		const auto format = ChooseSurfaceFormat();
		const auto presentMode = ChoosePresentMode();
		const auto extent = ChooseSurfaceExtent();

		VkSurfaceCapabilitiesKHR caps{};
		VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mDevice.GetGpu(), surf, &caps));

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
		swapchainInfo.surface = surf;
		swapchainInfo.preTransform = caps.currentTransform;

		VK_CHECK(vkCreateSwapchainKHR(dev, &swapchainInfo, nullptr, &mSwapchain));

		mSwapchainFormat = format.format;
		mSwapchainExtent = extent;

		// Get the images from the swapchain
		uint32_t swapImageCount{};
		VK_CHECK(vkGetSwapchainImagesKHR(dev, mSwapchain, &swapImageCount, nullptr));
		mSwapchainImages.resize(swapImageCount);
		VK_CHECK(vkGetSwapchainImagesKHR(dev, mSwapchain, &swapImageCount, mSwapchainImages.data()));

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
			VK_CHECK(vkCreateImageView(dev, &viewInfo, nullptr, &view));
			mSwapchainImageViews.emplace_back(view);
		}
	}

	void Swapchain::CleanUp()
	{
		for (const auto& view : mSwapchainImageViews)
			vkDestroyImageView(mDevice.Get(), view, nullptr);

		vkDestroySwapchainKHR(mDevice.Get(), mSwapchain, nullptr);

		mSwapchainImageViews.clear();
	}

	VkSurfaceFormatKHR Swapchain::ChooseSurfaceFormat()
	{
		const auto gpu = mDevice.GetGpu();
		const auto surf = mDevice.GetSurface();

		uint32_t formatCount{};
		VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surf, &formatCount, nullptr));
		assert(formatCount > 0 && "GPU has no surface formats available");
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surf, &formatCount, formats.data()));
		auto it = std::find_if(
			formats.begin(),
			formats.end(),
			[](const VkSurfaceFormatKHR& format) { return format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; }
		);

		if (it != formats.end())
			return *it;
		else
			return formats[0];
	}

	VkPresentModeKHR Swapchain::ChoosePresentMode()
	{
		const auto gpu = mDevice.GetGpu();
		const auto surf = mDevice.GetSurface();

		uint32_t modeCount{};
		VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surf, &modeCount, nullptr));
		assert(modeCount > 0 && "GPU has no present modes available");
		std::vector<VkPresentModeKHR> modes(modeCount);
		VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surf, &modeCount, nullptr));
		const auto it = std::find(modes.cbegin(), modes.cend(), VK_PRESENT_MODE_IMMEDIATE_KHR);
		if (it != modes.end())
			return *it;
		else
			return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D Swapchain::ChooseSurfaceExtent()
	{
		VkSurfaceCapabilitiesKHR caps{};
		VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mDevice.GetGpu(), mDevice.GetSurface(), &caps));
		if (caps.currentExtent.width != UINT32_MAX && caps.currentExtent.height != UINT32_MAX)
		{
			return caps.currentExtent;
		}
		else
		{
			VkExtent2D res{};
			glfwGetFramebufferSize(mDevice.GetWindow(), reinterpret_cast<int*>(&res.width), reinterpret_cast<int*>(&res.height));
			res.width = std::clamp(res.width, caps.minImageExtent.width, caps.maxImageExtent.width);
			res.height = std::clamp(res.height, caps.minImageExtent.height, caps.maxImageExtent.height);
			return res;
		}
	}
}

