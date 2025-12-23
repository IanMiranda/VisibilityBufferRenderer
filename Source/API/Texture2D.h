#pragma once

#include "Common.h"
#include "Texture.h"

namespace im
{
	class Device;

	class Texture2D : public Texture
	{
	public:
		Texture2D(Device& device, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height, uint32_t mipLevels);
		~Texture2D();

		Texture2D(const Texture2D& other) = delete;
		Texture2D& operator=(const Texture2D& other) = delete;

		VkImage Get() const { return mImage; }
		VkImageView GetView() const override { return mView; }
		VkFormat GetFormat() const { return mFormat; }
		uint32_t GetWidth() const { return mWidth; }
		uint32_t GetHeight() const { return mHeight; }
		VkExtent2D GetExtent() const { return { mWidth, mHeight }; }
		uint32_t GetMipLevels() const { return mMipLevelCount; }

		VkImageAspectFlags GetAspect() const;

	private:
		static bool IsDepthFormat(VkFormat format);
		static bool IsStencilFormat(VkFormat format);

	private:
		Device& mDevice;

		VkFormat mFormat;
		VkImageUsageFlags mUsage;
		uint32_t mWidth;
		uint32_t mHeight;
		uint32_t mMipLevelCount;

		VkImage mImage;
		VmaAllocation mAllocation;
		VkImageView mView;
	};
}