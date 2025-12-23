#pragma once

#include "Common.h"
#include "Texture.h"

namespace im
{
	class Device;

	class TextureCube : public Texture
	{
	public:
		TextureCube(Device& device, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height, bool createFaceViews = false, uint32_t mipLevels = 1);
		~TextureCube();

		TextureCube(const TextureCube& other) = delete;
		TextureCube& operator=(const TextureCube& other) = delete;

		VkImage Get() const { return mImage; }
		VkImageView GetView() const override { return mView; }
		VkImageView GetFaceView(uint32_t i, uint32_t mipLevel = 0) const { return mFaceViews[i + mipLevel * 6]; }
		VkFormat GetFormat() const { return mFormat; }
		uint32_t GetWidth() const { return mWidth; }
		uint32_t GetHeight() const { return mHeight; }

	private:
		Device& mDevice;

		VkFormat mFormat;
		VkImageUsageFlags mUsage;
		uint32_t mWidth;
		uint32_t mHeight;
		uint32_t mMipLevels;

		VkImage mImage;
		VmaAllocation mAllocation;
		VkImageView mView;
		std::vector<VkImageView> mFaceViews;
	};
}