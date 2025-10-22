#include "Shader.h"

#include "Device.h"
#include "Utils.h"

namespace im
{
	Shader::Shader(Device& device, const std::vector<char>& code)
		: mDevice(device)
	{
		VkShaderModuleCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		shaderInfo.codeSize = code.size();
		shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
		VK_CHECK(vkCreateShaderModule(mDevice.Get(), &shaderInfo, nullptr, &mShader));
	}
	Shader::Shader(Device& device, const std::filesystem::path& path)
		: Shader(device, utils::ReadFile(path))
	{
	}

	Shader::~Shader()
	{
		mDevice.WaitIdle();
		vkDestroyShaderModule(mDevice.Get(), mShader, nullptr);
	}

	Shader& Shader::AddStage(VkShaderStageFlagBits stage, const char* entrypoint)
	{
		VkPipelineShaderStageCreateInfo stageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stageInfo.module = mShader;
		stageInfo.pName = entrypoint;
		stageInfo.stage = stage;
		mStages.emplace_back(stageInfo);
		return *this;
	}
}
