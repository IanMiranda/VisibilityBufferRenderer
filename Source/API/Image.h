#pragma once

#include "Common.h"
#include "Device.h"

namespace im
{
	class Image
	{
	public:
		constexpr static uint32_t CubemapFaces = 6;
	public:
		Image(
			Device& device,
			VkFormat format,
			VkImageUsageFlags usage,
			uint32_t width,
			uint32_t height,
			uint32_t depth,
			uint32_t arrayLayers,
			VkImageType type,
			uint32_t mipLevels,
			VkImageCreateFlags flags = 0);
		~Image();
		
		Image(const Image& other) = delete;
		Image& operator=(const Image& other) = delete;

		VkImage Get() { return mImage; }
		VkFormat GetFormat() const { return mFormat; }
		uint32_t GetWidth() const { return mWidth; }
		uint32_t GetHeight() const { return mHeight; }
		VkExtent2D GetExtent() const { return { mWidth, mHeight }; }
		uint32_t GetDepth() const { return mDepth; }
		uint32_t GetMipLevels() const { return mMipLevels; }

		static uint32_t GetMaxMipLevels(uint32_t width, uint32_t height)
		{
			return std::floor(std::log2(std::max(width, height))) + 1;
		}

		static bool IsDepthFormat(VkFormat format);
		static bool IsStencilFormat(VkFormat format);

	private:
		Device& mDevice;
		VkFormat mFormat;
		uint32_t mWidth;
		uint32_t mHeight;
		uint32_t mDepth;
		uint32_t mMipLevels;

		VkImage mImage;
		VmaAllocation mAllocation;
	};
}