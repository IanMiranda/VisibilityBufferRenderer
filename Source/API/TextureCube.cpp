#include "TextureCube.h"

#include "Device.h"

namespace im
{
	TextureCube::TextureCube(Device& device, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height)
		: mDevice(device)
		, mFormat(format)
		, mUsage(usage)
		, mWidth(width)
		, mHeight(height)
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
	}

	TextureCube::~TextureCube()
	{
		mDevice.WaitIdle();

		vkDestroyImageView(mDevice.Get(), mView, nullptr);
		vmaDestroyImage(mDevice.GetAllocator(), mImage, mAllocation);
	}
}