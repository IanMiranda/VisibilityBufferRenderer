#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class TextureCube
	{
	public:
		TextureCube(Device& device, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height);
		~TextureCube();

		TextureCube(const TextureCube& other) = delete;
		TextureCube& operator=(const TextureCube& other) = delete;

		VkImage Get() const { return mImage; }
		VkImageView GetView() const { return mView; }
		VkFormat GetFormat() const { return mFormat; }
		uint32_t GetWidth() const { return mWidth; }
		uint32_t GetHeight() const { return mHeight; }

	private:
		Device& mDevice;

		VkFormat mFormat;
		VkImageUsageFlags mUsage;
		uint32_t mWidth;
		uint32_t mHeight;

		VkImage mImage;
		VmaAllocation mAllocation;
		VkImageView mView;
	};
}