#include "ShadowPass.h"

#include "Device.h"
#include "Texture2D.h"
#include "GraphicsPipeline.h"
#include "Utils.h"

namespace im
{
	ShadowPass::ShadowPass(Device& device, VkDeviceSize pushConstantSize, VkFormat depthFormat, VkExtent2D dims)
		: mDevice(device), mPushConstSize(pushConstantSize), mDepthFormat(depthFormat)
	{
		// stbi_set_flip_vertically_on_load(false); // Reversing UV coords using vp^-1, so images will be loaded in correct orientation

		const auto dev = mDevice.Get();
		const auto allocator = mDevice.GetAllocator();

		mShadowMap = std::make_unique<Texture2D>(mDevice, mDepthFormat,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, dims.width, dims.height, false);

		VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.compareEnable = VK_TRUE;
		samplerInfo.compareOp = VK_COMPARE_OP_LESS;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		VK_CHECK(vkCreateSampler(dev, &samplerInfo, nullptr, &mShadowMapSampler));

		const auto shaderSource = utils::ReadFile("Assets/Shaders/ShadowDepthPass.spv");
		VkShaderModule shader = utils::CreateShader(mDevice.Get(), shaderSource);

		VkPushConstantRange passDataRange{};
		passDataRange.offset = 0;
		passDataRange.size = sizeof(ShadowPassData);
		passDataRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		const std::vector<VkDescriptorSetLayout> setLayouts{};
		const std::vector<VkPushConstantRange> pcRanges{ passDataRange };
		mShadowPipeLayout = std::make_unique<PipelineLayout>(mDevice, setLayouts, pcRanges);

		VkVertexInputBindingDescription inputBinding{};
		inputBinding.binding = 0;
		inputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		inputBinding.stride = sizeof(Vertex);

		mShadowPipe = std::make_unique<GraphicsPipeline>(mDevice, *mShadowPipeLayout);
		mShadowPipe->AddShader(shader, VK_SHADER_STAGE_VERTEX_BIT, "VSMain")
			.SetVertexInput({ inputBinding }, { utils::InputAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0) })
			.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.SetRasterizer(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_POLYGON_MODE_FILL)
			.SetMsaaSamples(VK_SAMPLE_COUNT_1_BIT)
			.SetDepthAttachment(mDepthFormat, true)
			.Commit();

		vkDestroyShaderModule(dev, shader, nullptr);
	}

	ShadowPass::~ShadowPass()
	{
		vkDestroySampler(mDevice.Get(), mShadowMapSampler, nullptr);
	}

	void ShadowPass::Begin(VkCommandBuffer commandBuffer)
	{
		// First pass: shadow map generation
		mShadowMap->Barrier(commandBuffer,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, // WAR hazard - only need execution dep
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

		const VkExtent2D shadowMapExtent = mShadowMap->GetExtent();
		const VkRect2D shadowRenderArea = { { 0, 0 }, shadowMapExtent };

		const auto shadowAttachment = utils::RenderingDepthAttachment(
			mShadowMap->GetView(),
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE);

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.pDepthAttachment = &shadowAttachment;
		renderingInfo.renderArea = shadowRenderArea;
		renderingInfo.layerCount = 1;

		vkCmdBeginRendering(commandBuffer, &renderingInfo);

		const auto [viewport, scissor] = utils::ViewportAndScissor(shadowMapExtent);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPipe->Get());
	}

	void ShadowPass::End(VkCommandBuffer commandBuffer)
	{
		vkCmdEndRendering(commandBuffer);

		mShadowMap->Barrier(commandBuffer,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT);
	}
}

