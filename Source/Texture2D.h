#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class Texture2D
	{
	public:
		Texture2D(Device& device, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height, bool supportMipmaps);
		~Texture2D();

		Texture2D(const Texture2D& other) = delete;
		Texture2D& operator=(const Texture2D& other) = delete;

		VkImage Get() const { return mImage; }
		VkImageView GetView() const { return mView; }
		VkFormat GetFormat() const { return mFormat; }

		VkBufferImageCopy CopyFromBuffer() const;

		void Barrier(
			VkCommandBuffer commandBuffer,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		void Barrier(
			VkCommandBuffer commandBuffer,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
			uint32_t mipLevel);

		void GenerateMipmaps(VkCommandBuffer commandBuffer, VkImageLayout newLayout, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

	private:
		VkImageAspectFlags GetAspect() const;

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