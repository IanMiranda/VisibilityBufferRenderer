#include "VisibilityBufferBackend.h"

#include "Renderer.h"
#include "Scene.h"
#include "API/CommandBuffer.h"
#include "API/Buffer.h"
#include "API/RenderPass.h"
#include "API/Shader.h"

namespace im
{
	VisibilityBufferBackend::VisibilityBufferBackend(Renderer& renderer, size_t maxFramesInFlight, Image& depthImage)
		: mVisPipeLayout(
			renderer.GetDevice(),
			{},
			{ PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(VbPassData), 0) }
		)
		, mVisPipe(
			renderer.GetDevice(),
			GraphicsPipelineDesc(
				mVisPipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/VisibilityPass.spv")
				.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
				.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSMain"),
				{},
				InputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST),
				Rasterizer(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL),
				Multisample(VK_SAMPLE_COUNT_1_BIT),
				{ ColorAttachment(VK_FORMAT_R32G32_UINT) },
				{ DepthStencil(depthImage.GetFormat()) }
			)
		)
		, mVisBuffers(InitVisBuffers(renderer, maxFramesInFlight))
	{
	}

	void VisibilityBufferBackend::BeginScene(CommandBuffer& cmd, ImageView& depthView, uint32_t frameIndex)
	{

		cmd.SetViewportAndScissor(mVisBuffers[frameIndex].image->GetExtent());
		cmd.BeginRendering(
			{ ColorAttachment(mVisBuffers[frameIndex].view->Get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE) },
			DepthAttachment(depthView.Get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE),
			Scissor(mVisBuffers[frameIndex].image->GetExtent())
		);

		cmd.BindGraphicsPipeline(mVisPipe);
	}

	void VisibilityBufferBackend::DrawBatch(Scene& scene, CommandBuffer& cmd, const std::vector<VbObject>& objects)
	{
		const auto& camera = scene.GetCamera();
		const auto viewProj = camera.GetProjectionMatrix() * camera.GetViewMatrix();
		VbPassData passData{};

		for (const auto& object : objects)
		{
			passData.modelViewProj = viewProj * object.transform;
			passData.vertexData = object.mesh->vertexBuffer->GetAddress();

			cmd.PushConstants(mVisPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, passData);
			cmd.BindIndexBuffer(*object.mesh->indexBuffer);
			cmd.DrawIndexed(object.mesh->indexCount);
		}
	}

	void VisibilityBufferBackend::ResizeBuffers(Renderer& renderer, size_t maxFramesInFlight)
	{
		mVisBuffers = InitVisBuffers(renderer, maxFramesInFlight);
	}

	std::vector<Texture2D> VisibilityBufferBackend::InitVisBuffers(Renderer& renderer, size_t count)
	{
		std::vector<Texture2D> res;
		res.reserve(count);

		const auto extent = renderer.GetDevice().GetSwapchain().GetExtent();

		for (size_t i = 0; i < count; ++i) {
			Texture2D visBuf{};
			visBuf.image = std::make_unique<Image>(renderer.GetDevice(), VK_FORMAT_R32G32_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				extent.width, extent.height, 1, 1, VK_IMAGE_TYPE_2D, 1, 0);
			visBuf.view = std::make_unique<ImageView>(renderer.GetDevice(), *visBuf.image, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT,
				0, 1, 0, 1);
			res.emplace_back(std::move(visBuf));
		}

		return res;
	}
}