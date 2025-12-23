#include "TextureCube.h"

#include "Device.h"

namespace im
{
	TextureCube::TextureCube(
		Device& device,
		VkFormat format,
		VkImageUsageFlags usage,
		uint32_t width,
		uint32_t height,
		bool createFaceViews,
		uint32_t mipLevels)
		: mDevice(device)
		, mFormat(format)
		, mUsage(usage)
		, mWidth(width)
		, mHeight(height)
		, mMipLevels(mipLevels)
	{
		VkImageCreateInfo cubemapInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		cubemapInfo.arrayLayers = 6;
		cubemapInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		cubemapInfo.extent = { width, height, 1 };
		cubemapInfo.format = mFormat;
		cubemapInfo.imageType = VK_IMAGE_TYPE_2D;
		cubemapInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		cubemapInfo.mipLevels = 1;
		cubemapInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		cubemapInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		cubemapInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		cubemapInfo.usage = usage;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VK_CHECK(vmaCreateImage(mDevice.GetAllocator(), &cubemapInfo, &allocInfo, &mImage, &mAllocation, nullptr));

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.format = mFormat;
		viewInfo.image = mImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 6;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;

		VK_CHECK(vkCreateImageView(mDevice.Get(), &viewInfo, nullptr, &mView));

		if (createFaceViews)
		{
			mFaceViews.resize(6 * mMipLevels);
			for (uint32_t level = 0; level < mMipLevels; ++level)
			{
				viewInfo.subresourceRange.baseMipLevel = level;
				viewInfo.subresourceRange.levelCount = 1;
				for (uint32_t i = 0; i < 6; ++i)
				{
					viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
					viewInfo.subresourceRange.baseArrayLayer = i;
					viewInfo.subresourceRange.layerCount = 1;
					VK_CHECK(vkCreateImageView(mDevice.Get(), &viewInfo, nullptr, &mFaceViews[i]));
				}
			}
		}
	}

	TextureCube::~TextureCube()
	{
		mDevice.WaitIdle();

		for (auto view : mFaceViews)
			vkDestroyImageView(mDevice.Get(), view, nullptr);
		mFaceViews.clear();

		vkDestroyImageView(mDevice.Get(), mView, nullptr);
		vmaDestroyImage(mDevice.GetAllocator(), mImage, mAllocation);
	}
}