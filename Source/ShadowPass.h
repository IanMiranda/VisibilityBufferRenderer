#pragma once

#include "Common.h"
#include "PipelineLayout.h"

namespace im
{
	class Device;
	class Texture2D;
	class GraphicsPipeline;

	class ShadowPass
	{
	public:
		ShadowPass(Device& device, VkDeviceSize pushConstantSize, VkFormat depthFormat, VkExtent2D dims);
		~ShadowPass();

		void Begin(VkCommandBuffer commandBuffer);
		void End(VkCommandBuffer commandBuffer);

		Texture2D& GetMap() { return *mShadowMap; }
		VkSampler GetSampler() { return mShadowMapSampler; }
		PipelineLayout& GetLayout() { return *mShadowPipeLayout; }

	private:
		Device& mDevice;
		VkDeviceSize mPushConstSize;
		VkFormat mDepthFormat;

		std::unique_ptr<Texture2D> mShadowMap;
		VkSampler mShadowMapSampler{ VK_NULL_HANDLE };
		std::unique_ptr<PipelineLayout> mShadowPipeLayout;
		std::unique_ptr<GraphicsPipeline> mShadowPipe;

	};
}