#include "TextureCube.h"

#include "Device.h"

namespace im
{
	TextureCube::TextureCube(Device& device, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height)
		: mDevice(device)
		, mFormat(format)
		, mUsage(usage)
		, mWidth(width)
		, mHeight(height)
	{
		VkImageCreateInfo cubemapInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		cubemapInfo.arrayLayers = 6;
		cubemapInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		cubemapInfo.extent = { width, height, 1 };
		cubemapInfo.format = mFormat;
		cubemapInfo.imageType = VK_IMAGE_TYPE_2D;
		cubemapInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		cubemapInfo.mipLevels = 1;
		cubemapInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		cubemapInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		cubemapInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		cubemapInfo.usage = usage;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VK_CHECK(vmaCreateImage(mDevice.GetAllocator(), &cubemapInfo, &allocInfo, &mImage, &mAllocation, nullptr));

		VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.format = mFormat;
		viewInfo.image = mImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 6;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;

		VK_CHECK(vkCreateImageView(mDevice.Get(), &viewInfo, nullptr, &mView));
	}

	TextureCube::~TextureCube()
	{
		mDevice.WaitIdle();

		vkDestroyImageView(mDevice.Get(), mView, nullptr);
		vmaDestroyImage(mDevice.GetAllocator(), mImage, mAllocation);
	}

	VkBufferImageCopy TextureCube::CopyFromBuffer() const
	{
		VkBufferImageCopy buffer2Image{};
		buffer2Image.imageExtent = { mWidth, mHeight, 1 };
		buffer2Image.imageOffset = { 0, 0, 0 };
		buffer2Image.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		buffer2Image.imageSubresource.baseArrayLayer = 0;
		buffer2Image.imageSubresource.layerCount = 6;
		buffer2Image.imageSubresource.mipLevel = 0;
		buffer2Image.bufferImageHeight = 0;
		buffer2Image.bufferOffset = 0;
		buffer2Image.bufferRowLength = 0;
		return buffer2Image;
	}

	void TextureCube::Barrier(
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
		imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = 6;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(commandBuffer, &depInfo);
	}
}