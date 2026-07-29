#include "VisibilityBufferBackend.h"

#include "API/PipelineLayout.h"
#include "Renderer.h"
#include "Scene.h"
#include "API/CommandBuffer.h"
#include "API/Buffer.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "vulkan/vulkan_core.h"

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
				.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSVisibilityPass")
				.AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, "FSVisibilityPass"),
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
		DescriptorSetLayout buildDsl(renderer.GetDevice(), {
			DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
		});

		PipelineLayout buildLayout(renderer.GetDevice(), {
			buildDsl	
		}, {
			PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(VbBuildData), 0)
		});

		Shader buildShader(renderer.GetDevice(), "./Assets/Shaders/Bin/VisibilityWorklist.spv");
		buildShader.AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "CSBuildWorklist");

		VkComputePipelineCreateInfo buildPipeInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		buildPipeInfo.basePipelineHandle = VK_NULL_HANDLE;
		buildPipeInfo.layout = buildLayout.Get();
		buildPipeInfo.stage = buildShader.GetStages()[0];

		PipelineLayout sortLayout(renderer.GetDevice(), {}, {
			PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(VbSortData)),
		});

		Shader sortShader(renderer.GetDevice(), "./Assets/Shaders/Bin/VisibilitySort.spv");
		sortShader.AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "CSSortWorkList");

		VkComputePipelineCreateInfo sortPipeInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		sortPipeInfo.basePipelineHandle = VK_NULL_HANDLE;
		sortPipeInfo.layout = sortLayout.Get();
		sortPipeInfo.stage = sortShader.GetStages()[0];

		VkComputePipelineCreateInfo infos[] = { buildPipeInfo, sortPipeInfo };
		VkPipeline pipes[2];
		VK_CHECK(vkCreateComputePipelines(renderer.GetDevice().Get(), renderer.GetDevice().GetPipelineCache(), 1, infos, nullptr, pipes));
		vkDestroyPipeline(renderer.GetDevice().Get(), pipes[0], nullptr);
		vkDestroyPipeline(renderer.GetDevice().Get(), pipes[1], nullptr);
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
			cmd.DrawIndexed(object.mesh->indexCount, 1, 0, 0, currentInstance++);
		}
	}

	void VisibilityBufferBackend::End(Renderer& renderer, CommandBuffer& cmd)
	{
		cmd.EndRendering();
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

		renderer.GetDevice().RunImmediateCommands([&](CommandBuffer& cmd)
		{
			// TODO: global barrier?
			for (const auto& visBuf : res)
			{
				cmd.Barrier(
					*visBuf.image,
					VK_IMAGE_LAYOUT_UNDEFINED,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_NONE,
					VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT,
					0, 1, 0, 1
				);
			}
		});

		return res;
	}
}