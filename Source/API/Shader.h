#pragma once

#include <filesystem>

#include "Common.h"

namespace im
{
	class Device;

	class Shader
	{
		friend class GraphicsPipeline;
		friend class ComputePipeline;

	public:
		Shader(Device& device, const std::vector<char>& code);
		Shader(Device& device, const std::filesystem::path& path);
		~Shader();

		Shader(const Shader& other) = delete;
		Shader& operator=(const Shader& other) = delete;

		Shader& AddStage(VkShaderStageFlagBits stage, const char* entrypoint);

		std::vector<VkPipelineShaderStageCreateInfo>& GetStages() { return mStages; }

	private:
		Device& mDevice;

		VkShaderModule mShader;
		std::vector<VkPipelineShaderStageCreateInfo> mStages;
	};
}