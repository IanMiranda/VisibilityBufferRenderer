#pragma once

#include "Common.h"
#include "API/PipelineLayout.h"

namespace im
{
	class Device;
	class Texture2D;
	class GraphicsPipeline;
	class CommandBuffer;

	class ShadowPass
	{
	public:
		ShadowPass(Device& device, VkDeviceSize pushConstantSize, VkExtent2D dims);
		~ShadowPass();

		void Begin(CommandBuffer& cmds);
		void End(CommandBuffer& cmds);

		Texture2D& GetMap() { return *mShadowMap; }
		VkSampler GetSampler() { return mShadowMapSampler; }
		PipelineLayout& GetLayout() { return *mShadowPipeLayout; }

	private:
		Device& mDevice;
		const VkDeviceSize mPushConstSize;

		std::unique_ptr<Texture2D> mShadowMap;
		VkSampler mShadowMapSampler{ VK_NULL_HANDLE };
		std::unique_ptr<PipelineLayout> mShadowPipeLayout;
		std::unique_ptr<GraphicsPipeline> mShadowPipe;

	};
}