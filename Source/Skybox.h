#pragma once

#include "Common.h"

#include "API/TextureCube.h"
#include "API/DescriptorSetLayout.h"
#include "API/PipelineLayout.h"
#include "API/GraphicsPipeline.h"
#include "API/DescriptorPool.h"
#include "API/DescriptorSet.h"

namespace im
{
	class Device;
	class CommandBuffer;

	class Skybox
	{
	public:
		static constexpr uint32_t Faces = 6;

	public:
		Skybox(Device& device, const std::array<std::filesystem::path, Faces>& skyboxPaths);
		
		TextureCube& Get() { return *mEnvMap; }

		void Draw(CommandBuffer& cmds, const glm::mat4& view, const glm::mat4& proj);

	private:
		std::unique_ptr<TextureCube> mEnvMap;
		std::unique_ptr<DescriptorSetLayout> mEnvMapSetLayout;
		std::unique_ptr<PipelineLayout> mEnvMapPipeLayout;
		std::unique_ptr<GraphicsPipeline> mEnvMapPipe;
		std::unique_ptr<DescriptorPool> mEnvMapPool;
		std::unique_ptr<DescriptorSet> mEnvMapSet;
	};
}