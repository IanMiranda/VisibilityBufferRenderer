#include "Image.h"

namespace im
{
    Image::Image(
        Device& device,
        VkFormat format,
        VkImageUsageFlags usage,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t arrayLayers,
        VkImageType type,
        uint32_t mipLevels,
        VkImageCreateFlags flags)
        : mDevice(device)
        , mFormat(format)
        , mWidth(width)
        , mHeight(height)
        , mDepth(depth)
        , mMipLevels(mipLevels)
    {
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		imageInfo.arrayLayers = arrayLayers;
		imageInfo.extent = { width, height, depth };
		imageInfo.format = format;
		imageInfo.imageType = type;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.mipLevels = mipLevels;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = usage;
        imageInfo.flags = flags;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VK_CHECK(vmaCreateImage(mDevice.GetAllocator(), &imageInfo, &allocInfo, &mImage, &mAllocation, nullptr));
    }

    Image::~Image()
    {
        mDevice.WaitIdle();

        vmaDestroyImage(mDevice.GetAllocator(), mImage, mAllocation);
    }
    
    bool Image::IsDepthFormat(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D16_UNORM:
            return true;
        }
        
        return false;

    }

    bool Image::IsStencilFormat(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
            return true;
        }
        
        return false;
    }
}
