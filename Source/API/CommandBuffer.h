#pragma once

#include "Common.h"
#include "PipelineLayout.h"

namespace im
{
	class Device;
	class CommandPool;
	class Texture2D;
	class TextureCube;
	class GraphicsPipeline;
	class PipelineLayout;
	class DescriptorSet;
	class Buffer;

	class CommandBuffer
	{
	public:
		CommandBuffer(CommandPool& pool, VkCommandBuffer buffer);
		~CommandBuffer();

		CommandBuffer(const CommandBuffer& other) = delete;
		CommandBuffer& operator=(const CommandBuffer& other) = delete;

		void Begin(VkCommandBufferUsageFlags usage = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		void End();

		void BarrierSwapchainImage(
			VkImage image,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		void Barrier(
			Texture2D& texture,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		void Barrier(
			Texture2D& texture,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
			uint32_t mipLevel);

		void Barrier(
			TextureCube& texture,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		void GenerateMipmaps(Texture2D& texture, VkImageLayout newLayout, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		void Copy(Buffer& src, Buffer& dst);
		void Copy(Buffer& src, Buffer& dst, VkDeviceSize size);
		void Copy(Buffer& src, Texture2D& dst);
		void Copy(Buffer& src, TextureCube& dst);

		void SetViewportAndScissor(VkViewport viewport, VkRect2D scissor);
		void SetViewportAndScissor(VkExtent2D renderArea);

		void BeginRendering(
			const std::vector<VkRenderingAttachmentInfo>& colorAttachments,
			VkRect2D renderArea,
			uint32_t layers = 1);
		void BeginRendering(
			const std::vector<VkRenderingAttachmentInfo>& colorAttachments,
			VkRenderingAttachmentInfo depthAttachment,
			VkRect2D renderArea,
			uint32_t layers = 1);
		void EndRendering();

		template <typename T>
		void PushConstants(PipelineLayout& layout, VkShaderStageFlags stage, const T& data, uint32_t offset = 0)
		{
			vkCmdPushConstants(mCmdBuf, layout.Get(), stage, offset, sizeof(data), &data);
		}

		void BindGraphicsDescriptorSets(
			PipelineLayout& layout,
			uint32_t firstSet,
			const std::vector<std::reference_wrapper<DescriptorSet>>& sets);

		void BindGraphicsPipeline(GraphicsPipeline& pipeline);
		void BindVertexBuffer(Buffer& vertexBuffer);
		void BindIndexBuffer(Buffer& indexBuffer, VkIndexType indexType = VK_INDEX_TYPE_UINT32);

		void Draw(
			uint32_t vertexCount,
			uint32_t instanceCount = 1,
			uint32_t firstVertex = 0,
			uint32_t firstInstance = 0);

		void DrawIndexed(
			uint32_t indexCount,
			uint32_t instanceCount = 1,
			uint32_t firstIndex = 0,
			uint32_t vertexOffet = 0,
			uint32_t firstInstance = 0);

		void DrawIndexedIndirect(Buffer& buffer, uint32_t offset, uint32_t drawCount, uint32_t stride);

		VkCommandBuffer Get() { return mCmdBuf; }

	private:
		CommandPool& mPool;

		VkCommandBuffer mCmdBuf;
	};
}