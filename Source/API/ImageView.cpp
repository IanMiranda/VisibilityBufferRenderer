#include "ImageView.h"

#include "Image.h"

namespace im
{
    ImageView::ImageView(
        Device& device,
        Image& image,
        VkImageViewType type,
        VkImageAspectFlags aspect,
        uint32_t firstLayer, uint32_t layerCount,
        uint32_t firstLevel, uint32_t levelCount
    )
        : mDevice(device)
        , mImage(image)
        , mAspect(aspect)
    {
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.format = mImage.GetFormat();
		viewInfo.image = mImage.Get();
		viewInfo.viewType = type;
		viewInfo.subresourceRange.aspectMask = aspect;
		viewInfo.subresourceRange.baseArrayLayer = firstLayer;
		viewInfo.subresourceRange.layerCount = layerCount;
		viewInfo.subresourceRange.baseMipLevel = firstLevel;
		viewInfo.subresourceRange.levelCount = levelCount;

		VK_CHECK(vkCreateImageView(mDevice.Get(), &viewInfo, nullptr, &mView));
    }

    ImageView::~ImageView()
    {
        mDevice.WaitIdle();

		vkDestroyImageView(mDevice.Get(), mView, nullptr);
    }
    
    std::vector<std::unique_ptr<ImageView>> ImageView::CreateFacesForCubemap(Device &device, Image &image, VkImageAspectFlags aspect)
    {
        std::vector<std::unique_ptr<ImageView>> res;
        res.reserve(6);
        for (uint32_t i = 0; i < 6; ++i)
		{
            res.emplace_back(std::make_unique<ImageView>(device, image, VK_IMAGE_VIEW_TYPE_2D, aspect, i, 1, 0, image.GetMipLevels()));
		}
        return res;
    }

    std::vector<std::unique_ptr<ImageView>> ImageView::CreateFacesForCubemap(Device &device, Image &image, VkImageAspectFlags aspect, uint32_t firstLevel, uint32_t levelCounts)
    {
        std::vector<std::unique_ptr<ImageView>> res;
        res.reserve(6 * image.GetMipLevels());
        for (uint32_t level = 0; level < image.GetMipLevels(); ++level)
		{
			for (uint32_t i = 0; i < 6; ++i)
			{
                res.emplace_back(std::make_unique<ImageView>(device, image, VK_IMAGE_VIEW_TYPE_2D, aspect, i, 1, level, 1));
			}
		}
        return res;
    }
}
