#include "ShadowPass.h"

#include "API/Device.h"
#include "API/Texture2D.h"
#include "API/Shader.h"
#include "API/GraphicsPipeline.h"
#include "API/CommandBuffer.h"
#include "Utils.h"

namespace im
{
	ShadowPass::ShadowPass(Device& device, VkDeviceSize pushConstantSize, VkExtent2D dims)
		: mDevice(device), mPushConstSize(pushConstantSize)
	{
		mShadowMap = std::make_unique<Texture2D>(
			mDevice, mDevice.GetDepthFormat(),
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			dims.width, dims.height, false
		);

		Shader shader(mDevice, "Assets/Shaders/ShadowDepthPass.spv");
		shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, "VSMain");

		VkPushConstantRange passDataRange{};
		passDataRange.offset = 0;
		passDataRange.size = sizeof(ShadowPassData);
		passDataRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		mShadowPipeLayout = std::make_unique<PipelineLayout>(
			mDevice,
			std::initializer_list<std::reference_wrapper<DescriptorSetLayout>>{},
			std::initializer_list{ passDataRange }
		);

		mShadowPipe = std::make_unique<GraphicsPipeline>(mDevice, *mShadowPipeLayout, shader);
		mShadowPipe->SetVertexInput(
				{ utils::InputBinding(0, VK_VERTEX_INPUT_RATE_VERTEX, sizeof(Vertex)) },
				{ utils::InputAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0) }
			)
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.SetDepthAttachment(mShadowMap->GetFormat(), true)
			.Commit();
	}

	void ShadowPass::Begin(CommandBuffer& cmds)
	{
		// First pass: shadow map generation
		cmds.Barrier(*mShadowMap,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, // WAR hazard - only need execution dep
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

		cmds.BeginRendering(
			{},
			utils::DepthAttachment(
				mShadowMap->GetView(),
				VK_ATTACHMENT_LOAD_OP_CLEAR,
				VK_ATTACHMENT_STORE_OP_STORE
			),
			utils::Scissor(mShadowMap->GetExtent())
		);

		cmds.SetViewportAndScissor(mShadowMap->GetExtent());

		cmds.BindGraphicsPipeline(*mShadowPipe);
	}

	void ShadowPass::End(CommandBuffer& cmds)
	{
		cmds.EndRendering();

		cmds.Barrier(*mShadowMap,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT);
	}
}

