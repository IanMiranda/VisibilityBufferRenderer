#include "CommandBuffer.h"

#include "CommandPool.h"
#include "Device.h"
#include "Buffer.h"
#include "Image.h"
#include "PipelineLayout.h"
#include "DescriptorSet.h"
#include "GraphicsPipeline.h"
#include "Utils.h"

namespace im
{
	CommandBuffer::CommandBuffer(CommandPool& pool, VkCommandBuffer buffer)
		: mPool(pool), mCmdBuf(buffer)
	{
	}

	CommandBuffer::~CommandBuffer()
	{
		vkFreeCommandBuffers(mPool.GetDevice().Get(), mPool.Get(), 1, &mCmdBuf);
	}

	void CommandBuffer::Begin(VkCommandBufferUsageFlags usage)
	{
		VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
		beginInfo.flags = usage;

		VK_CHECK(vkBeginCommandBuffer(mCmdBuf, &beginInfo));
	}

	void CommandBuffer::End()
	{
		VK_CHECK(vkEndCommandBuffer(mCmdBuf));
	}

    void CommandBuffer::BarrierSwapchainImage(
		VkImage image,
		VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
    {
		VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.image = image;
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
		imageBarrier.subresourceRange.layerCount = 1;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = 1;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(mCmdBuf, &depInfo);
    }

    void CommandBuffer::Barrier(
		Image& texture,
		VkImageLayout oldLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
		VkImageAspectFlags aspect,
		uint32_t firstLayer, uint32_t layerCount,
		uint32_t firstLevel, uint32_t levelCount
	)
	{
		VkImageMemoryBarrier2 imageBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
		imageBarrier.image = texture.Get();
		imageBarrier.oldLayout = oldLayout;
		imageBarrier.newLayout = newLayout;
		imageBarrier.srcStageMask = srcStage;
		imageBarrier.srcAccessMask = srcAccess;
		imageBarrier.dstStageMask = dstStage;
		imageBarrier.dstAccessMask = dstAccess;
		imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageBarrier.subresourceRange.aspectMask = aspect;
		imageBarrier.subresourceRange.baseArrayLayer = firstLayer;
		imageBarrier.subresourceRange.layerCount = layerCount;
		imageBarrier.subresourceRange.baseMipLevel = firstLevel;
		imageBarrier.subresourceRange.levelCount = levelCount;

		VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(mCmdBuf, &depInfo);
	}

	void CommandBuffer::GenerateMipmaps(
		Image& texture, VkImageLayout newLayout,
		VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
		VkImageAspectFlags aspect
	)
	{
		VkFormatProperties props{};
		vkGetPhysicalDeviceFormatProperties(mPool.GetDevice().GetGpu(), texture.GetFormat(), &props);
		if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		{
			fmt::println(stderr, "Failed to generate mipmaps, image does not support linear blit!");
			return;
		}

		int currentWidth = texture.GetWidth();
		int currentHeight = texture.GetHeight();

		for (int i = 1; i < texture.GetMipLevels(); ++i)
		{
			Barrier(texture,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				aspect, 0, 1,
				i - 1, 1
			);

			VkImageBlit blit{};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { currentWidth, currentHeight, 1 };
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { currentWidth > 1 ? currentWidth / 2 : 1, currentHeight > 1 ? currentHeight / 2 : 1, 1 };
			blit.srcSubresource.aspectMask = aspect;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.srcSubresource.mipLevel = i - 1;
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;
			blit.dstSubresource.mipLevel = i;

			vkCmdBlitImage(mCmdBuf, texture.Get(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.Get(),
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

			Barrier(texture,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, newLayout,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
				dstStage, dstAccess, aspect, 0, 1, i - 1, 1);

			if (currentWidth > 1) currentWidth /= 2;
			if (currentHeight > 1) currentHeight /= 2;
		}

		Barrier(texture,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, newLayout,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			dstStage, dstAccess, aspect, 0, 1, texture.GetMipLevels() - 1, 1
		);
	}

	void CommandBuffer::Copy(Buffer& src, Buffer& dst)
	{
		Copy(src, dst, src.GetSize());
	}

	void CommandBuffer::Copy(Buffer& src, Buffer& dst, VkDeviceSize size)
	{
		VkBufferCopy copy{};
		copy.size = size;
		copy.srcOffset = 0;
		copy.dstOffset = 0;
		vkCmdCopyBuffer(mCmdBuf, src.Get(), dst.Get(), 1, &copy);
	}

	void CommandBuffer::Copy(
		Buffer& src, Image& dst,
		VkImageAspectFlags aspect,
		uint32_t firstLayer, uint32_t layerCount,
		uint32_t mipLevel
	)
	{
		VkBufferImageCopy buffer2Image{};
		buffer2Image.imageExtent = { dst.GetWidth(), dst.GetHeight(), dst.GetDepth()};
		buffer2Image.imageOffset = { 0, 0, 0 };
		buffer2Image.imageSubresource.aspectMask = aspect;
		buffer2Image.imageSubresource.baseArrayLayer = firstLayer;
		buffer2Image.imageSubresource.layerCount = layerCount;
		buffer2Image.imageSubresource.mipLevel = mipLevel;
		buffer2Image.bufferImageHeight = 0;
		buffer2Image.bufferOffset = 0;
		buffer2Image.bufferRowLength = 0;
		vkCmdCopyBufferToImage(mCmdBuf, src.Get(), dst.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer2Image);
	}

	void CommandBuffer::SetViewportAndScissor(VkViewport viewport, VkRect2D scissor)
	{
		vkCmdSetViewport(mCmdBuf, 0, 1, &viewport);
		vkCmdSetScissor(mCmdBuf, 0, 1, &scissor);
	}

	void CommandBuffer::SetViewportAndScissor(VkExtent2D renderArea)
	{
		const auto [viewport, scissor] = utils::ViewportAndScissor(renderArea);
		SetViewportAndScissor(viewport, scissor);
	}

	void CommandBuffer::BeginRendering(const std::vector<VkRenderingAttachmentInfo>& colorAttachments, VkRect2D renderArea, uint32_t layers)
	{
		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.colorAttachmentCount = colorAttachments.size();
		renderingInfo.pColorAttachments = colorAttachments.data();
		renderingInfo.renderArea = renderArea;
		renderingInfo.layerCount = layers;
		vkCmdBeginRendering(mCmdBuf, &renderingInfo);
	}

	void CommandBuffer::BeginRendering(const std::vector<VkRenderingAttachmentInfo>& colorAttachments, VkRenderingAttachmentInfo depthAttachment, VkRect2D renderArea, uint32_t layers)
	{
		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.colorAttachmentCount = colorAttachments.size();
		renderingInfo.pColorAttachments = colorAttachments.data();
		renderingInfo.pDepthAttachment = &depthAttachment;
		renderingInfo.renderArea = renderArea;
		renderingInfo.layerCount = layers;
		vkCmdBeginRendering(mCmdBuf, &renderingInfo);
	}

	void CommandBuffer::EndRendering()
	{
		vkCmdEndRendering(mCmdBuf);
	}

	void CommandBuffer::BindGraphicsDescriptorSets(
		PipelineLayout& layout,
		uint32_t firstSet,
		const std::vector<std::reference_wrapper<DescriptorSet>>& sets)
	{
		std::vector<VkDescriptorSet> vulkanSets;
		vulkanSets.reserve(sets.size());
		for (const auto& set : sets)
			vulkanSets.emplace_back(set.get().Get());
		
		vkCmdBindDescriptorSets(
			mCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
			layout.Get(),
			firstSet,
			vulkanSets.size(), vulkanSets.data(),
			0, nullptr);
	}

	void CommandBuffer::BindGraphicsPipeline(GraphicsPipeline& pipeline)
	{
		vkCmdBindPipeline(mCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.Get());
	}

    void CommandBuffer::BindVertexBuffer(Buffer& vertexBuffer)
	{
		const VkDeviceSize offsets[] = { 0 };
		const VkBuffer buffer = vertexBuffer.Get();
		vkCmdBindVertexBuffers(mCmdBuf, 0, 1, &buffer, offsets);
	}

	void CommandBuffer::BindIndexBuffer(Buffer& indexBuffer, VkIndexType indexType)
	{
		vkCmdBindIndexBuffer(mCmdBuf, indexBuffer.Get(), 0, indexType);
	}

	void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
	{
		vkCmdDraw(mCmdBuf, vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffet, uint32_t firstInstance)
	{
		vkCmdDrawIndexed(mCmdBuf, indexCount, instanceCount, firstIndex, vertexOffet, firstInstance);
	}

	void CommandBuffer::DrawIndexedIndirect(Buffer& buffer, uint32_t offset, uint32_t drawCount, uint32_t stride)
	{
		vkCmdDrawIndexedIndirect(mCmdBuf, buffer.Get(), offset, drawCount, stride);
	}
}
