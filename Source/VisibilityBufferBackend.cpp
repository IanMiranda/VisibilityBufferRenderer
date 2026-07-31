#include "VisibilityBufferBackend.h"

#include "API/PipelineLayout.h"
#include "Renderer.h"
#include "Scene.h"
#include "API/CommandBuffer.h"
#include "API/Buffer.h"
#include "API/RenderPass.h"
#include "API/Shader.h"
#include "API/Swapchain.h"

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
		, mBindlessSet(renderer.GetDevice())
		, mWorkListDsl(
			renderer.GetDevice(),
			{ DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT) }
		)
		, mWorkListPipeLayout(
			renderer.GetDevice(),
			{ std::ref(mWorkListDsl) },
			{ PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(VbBuildData), 0) }
		)
		, mWorkListPipe(
			renderer.GetDevice(),
			ComputePipelineDesc(
				mWorkListPipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/VisibilityWorklist.spv")
					.AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "CSBuildWorklist")
			)
		)
		, mSortDsl(
			renderer.GetDevice(),
			{ DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT) }
		)
		, mSortPipeLayout(
			renderer.GetDevice(),
			{ std::ref(mSortDsl) },
			{ PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(VbSortData), 0) }
		)
		, mSortPipe(
			renderer.GetDevice(),
			ComputePipelineDesc(
				mSortPipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/VisibilitySort.spv")
					.AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "CSSortWorkList")
			)
		)
		, mShadeDsl(
			renderer.GetDevice(),
			{ DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT) }
		)
		, mShadePipeLayout(
			renderer.GetDevice(),
			{ std::ref(mShadeDsl), std::ref(mBindlessSet.GetSetLayout()) },
			{ PushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(VbShadingData)) }
		)
		, mShadePipe(
			renderer.GetDevice(),
			ComputePipelineDesc(
				mShadePipeLayout,
				Shader(renderer.GetDevice(), "./Assets/Shaders/Bin/VisibilityShade.spv")
					.AddStage(VK_SHADER_STAGE_COMPUTE_BIT, "CSVisibilityShading")
			)
		)
		, mVisBuffers(InitVisBuffers(renderer, maxFramesInFlight))
		, mInstanceToShaderIdMaps(InitBuffers(
			renderer,
			maxFramesInFlight,
			BufferDesc(
				sizeof(uint32_t) * MaxDrawCalls,
				VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT,
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			)
		))
		, mWorkListCounters(InitBuffers(
			renderer,
			maxFramesInFlight,
			BufferDesc(
				sizeof(uint32_t),
				VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
			)
		))
		, mWorkLists(InitBuffers(
			renderer,
			maxFramesInFlight,
			BufferDesc(
				MaxShaders * GetTileCount(renderer.GetDevice().GetSwapchain().GetExtent()) * sizeof(VbWorkItem),
				VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
			)
		))
		, mShaderIdToTileCounts(InitBuffers(
			renderer,
			maxFramesInFlight,
			BufferDesc(
				MaxShaders * sizeof(uint32_t),
				VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT
			)
		))
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
			cmd.DrawIndexed(object.mesh->indexCount, 1, 0, 0, currentInstance++);
		}
	}

	void VisibilityBufferBackend::End(Renderer& renderer, CommandBuffer& cmd, uint32_t frameIndex)
	{
		cmd.EndRendering();

		// Now, build the worklist and sort it
		VbBuildData buildData{};
		buildData.windowSize = glm::uvec2(
			renderer.GetDevice().GetSwapchain().GetExtent().width,
			renderer.GetDevice().GetSwapchain().GetExtent().height
		);
		buildData.instanceToShaderIdMap = mInstanceToShaderIdMaps[frameIndex]->GetAddress();
		buildData.workListCounter = mWorkListCounters[frameIndex]->GetAddress();
		buildData.workList = mWorkLists[frameIndex]->GetAddress();
		buildData.shaderIdToTileCount = mShaderIdToTileCounts[frameIndex]->GetAddress();

		cmd.PushConstants(mWorkListPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, buildData);
		cmd.Dispatch(
			(buildData.windowSize.x + TileSize.x - 1) / TileSize.x, // Ceiling
			(buildData.windowSize.y + TileSize.y - 1) / TileSize.y,
			1
		);

		VbSortData sortData{};
		sortData.worklistCounter = mWorkListCounters[frameIndex]->GetAddress();
		sortData.workList = mWorkLists[frameIndex]->GetAddress();
		sortData.shaderIdToTileCount = mShaderIdToTileCounts[frameIndex]->GetAddress();
		sortData.offsetTable = ...;
		sortData.tileBuffer = ...
		sortData.windowSize = buildData.windowSize;

		cmd.PushConstants(mSortPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, sortData);
		cmd.Barrier({
			MemoryBarrier(
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT
			)
		});

		cmd.Dispatch(GroupSize, 1, 1);

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

    std::vector<std::unique_ptr<Buffer>> VisibilityBufferBackend::InitBuffers(Renderer& renderer, size_t count, const BufferDesc& desc)
    {
        std::vector<std::unique_ptr<Buffer>> res;
		res.reserve(count);

		for (size_t i = 0; i < count; ++i)
		{
			res.emplace_back(std::make_unique<Buffer>(renderer.GetDevice(), desc));
		}

		return res;
    }
}