#include "Texture2D.h"

#include <ktx.h>

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

	VkBufferImageCopy Texture2D::CopyFromBuffer() const
	{
		VkBufferImageCopy buffer2Image{};
		buffer2Image.imageExtent = { mWidth, mHeight, 1 };
		buffer2Image.imageOffset = { 0, 0, 0 };
		buffer2Image.imageSubresource.aspectMask = GetAspect();
		buffer2Image.imageSubresource.baseArrayLayer = 0;
		buffer2Image.imageSubresource.layerCount = 1;
		buffer2Image.imageSubresource.mipLevel = 0;
		buffer2Image.bufferImageHeight = 0;
		buffer2Image.bufferOffset = 0;
		buffer2Image.bufferRowLength = 0;
		return buffer2Image;
	}

	void Texture2D::Barrier(
		VkCommandBuffer commandBuffer,
		VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
	{
		VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.image = mImage;
		imageBarrier.oldLayout = oldLayout;
		imageBarrier.newLayout = newLayout;
		imageBarrier.srcStageMask = srcStage;
		imageBarrier.srcAccessMask = srcAccess;
		imageBarrier.dstStageMask = dstStage;
		imageBarrier.dstAccessMask = dstAccess;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.subresourceRange.aspectMask = GetAspect();
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = mMipLevelCount;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(commandBuffer, &depInfo);
	}

	void Texture2D::Barrier(
		VkCommandBuffer commandBuffer,
		VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
		uint32_t mipLevel)
	{
		VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.image = mImage;
		imageBarrier.oldLayout = oldLayout;
		imageBarrier.newLayout = newLayout;
		imageBarrier.srcStageMask = srcStage;
		imageBarrier.srcAccessMask = srcAccess;
		imageBarrier.dstStageMask = dstStage;
		imageBarrier.dstAccessMask = dstAccess;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.subresourceRange.aspectMask = GetAspect();
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 1;
		imageBarrier.subresourceRange.baseMipLevel = mipLevel;
		imageBarrier.subresourceRange.levelCount = 1;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(commandBuffer, &depInfo);
	}

	void Texture2D::GenerateMipmaps(VkCommandBuffer commandBuffer, VkImageLayout newLayout, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
	{
		VkFormatProperties props{};
		vkGetPhysicalDeviceFormatProperties(mDevice.GetGpu(), mFormat, &props);
		if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		{
			std::cerr << "Failed to generate mipmaps, image does not support linear blit!\n";
			return;
		}

		int currentWidth = mWidth;
		int currentHeight = mHeight;

		for (int i = 1; i < mMipLevelCount; ++i)
		{
			Barrier(commandBuffer,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				i - 1);

			VkImageBlit blit{};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { currentWidth, currentHeight, 1 };
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { currentWidth > 1 ? currentWidth / 2 : 1, currentHeight > 1 ? currentHeight / 2 : 1, 1 };
			blit.srcSubresource.aspectMask = GetAspect();
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.srcSubresource.mipLevel = i - 1;
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;
			blit.dstSubresource.mipLevel = i;

			vkCmdBlitImage(commandBuffer, mImage,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mImage,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			Barrier(commandBuffer,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, newLayout,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				dstStage, dstAccess, i - 1);

			if (currentWidth > 1) currentWidth /= 2;
			if (currentHeight > 1) currentHeight /= 2;
		}

		Barrier(commandBuffer,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, newLayout,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			dstStage, dstAccess, mMipLevelCount - 1);
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