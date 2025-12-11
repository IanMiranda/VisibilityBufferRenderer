#include "Texture2D.h"

// #include <ktx.h>

#include "Device.h"
#include "TextureCube.h"
#include "Buffer.h"

namespace im
{
	Texture2D::Texture2D(Device& device, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height, bool supportMipmaps)
		: mDevice(device)
		, mFormat(format)
		, mUsage(usage)
		, mWidth(width)
		, mHeight(height)
		, mMipLevelCount(supportMipmaps ? static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1 : 1)
	{
		VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.arrayLayers = 1;
		imageInfo.extent = { mWidth, mHeight, 1 };
		imageInfo.format = mFormat;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.mipLevels = mMipLevelCount;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = mUsage;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VK_CHECK(vmaCreateImage(mDevice.GetAllocator(), &imageInfo, &allocInfo, &mImage, &mAllocation, nullptr));

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.format = imageInfo.format;
		viewInfo.image = mImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.subresourceRange.aspectMask = GetAspect();
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mMipLevelCount;

		VK_CHECK(vkCreateImageView(mDevice.Get(), &viewInfo, nullptr, &mView));
	}

	Texture2D::~Texture2D()
	{
		mDevice.WaitIdle();
		
		vkDestroyImageView(mDevice.Get(), mView, nullptr);
		vmaDestroyImage(mDevice.GetAllocator(), mImage, mAllocation);
	}

	VkImageAspectFlags Texture2D::GetAspect() const
	{
		return IsDepthFormat(mFormat)
			? VK_IMAGE_ASPECT_DEPTH_BIT | (IsStencilFormat(mFormat) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)
			: VK_IMAGE_ASPECT_COLOR_BIT;
	}

	bool Texture2D::IsDepthFormat(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT
			|| format == VK_FORMAT_D32_SFLOAT_S8_UINT
			|| format == VK_FORMAT_D24_UNORM_S8_UINT
			|| format == VK_FORMAT_D16_UNORM_S8_UINT
			|| format == VK_FORMAT_D16_UNORM;
	}
	bool Texture2D::IsStencilFormat(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT
			|| format == VK_FORMAT_D24_UNORM_S8_UINT
			|| format == VK_FORMAT_D16_UNORM_S8_UINT;
	}
}