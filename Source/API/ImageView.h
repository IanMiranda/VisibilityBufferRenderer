#pragma once

#include "Common.h"
#include "Device.h"
#include "Image.h"

namespace im
{
    class ImageView
    {
    public:
        ImageView(
            Device& device,
            Image& image,
            VkImageViewType type,
            VkImageAspectFlags aspect,
            uint32_t firstLayer, uint32_t layerCount,
            uint32_t firstLevel, uint32_t levelCount);
        ~ImageView();

        ImageView(const ImageView& other) = delete;
		ImageView& operator=(const ImageView& other) = delete;

        VkImageView Get() { return mView; }
        const VkImageView& Get() const { return mView; }
        VkImageAspectFlags GetAspect() const { return mAspect; }

        static std::vector<std::unique_ptr<ImageView>> CreateFacesForCubemap(
            Device& device,
            Image& image,
            VkImageAspectFlags aspect);
        
        static std::vector<std::unique_ptr<ImageView>> CreateFacesForCubemap(
            Device& device,
            Image& image,
            VkImageAspectFlags aspect,
            uint32_t firstLevel, uint32_t levelCounts);

    private:
        Device& mDevice;
        Image& mImage;
        VkImageAspectFlags mAspect;

        VkImageView mView;
    };

    struct Texture2D
    {
        std::unique_ptr<Image> image;
        std::unique_ptr<ImageView> view;
    };

    struct TextureCube
    {
        std::unique_ptr<Image> image;
        std::unique_ptr<ImageView> view;
    };

    struct RenderTextureCube
    {
        std::unique_ptr<Image> image;
        std::array<std::unique_ptr<ImageView>, Image::CubemapFaces> views;
    };
}