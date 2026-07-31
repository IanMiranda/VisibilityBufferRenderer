#include "ComputePipeline.h"

namespace im
{
    ComputePipeline::ComputePipeline(Device& device, const ComputePipelineDesc& desc)
        : mDevice(device)
    {
        VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        info.basePipelineHandle = VK_NULL_HANDLE;
        info.layout = desc.mLayout.Get();
        info.stage = desc.mShader.mStages[0];

        VK_CHECK(vkCreateComputePipelines(mDevice.Get(), mDevice.GetPipelineCache(), 1, &info, nullptr, &mPipeline));
    }

    ComputePipeline::~ComputePipeline()
    {
        vkDestroyPipeline(mDevice.Get(), mPipeline, nullptr);
    }

    std::vector<std::unique_ptr<ComputePipeline>> ComputePipeline::Create(Device& device, const std::vector<ComputePipelineDesc>& descs)
    {
        std::vector<VkComputePipelineCreateInfo> infos;
        infos.reserve(descs.size());

        for (const auto& desc : descs)
        {
            VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            info.basePipelineHandle = VK_NULL_HANDLE;
            info.layout = desc.mLayout.Get();
            info.stage = desc.mShader.mStages[0];
            
            infos.emplace_back(info);
        }

        std::vector<VkPipeline> pipes(infos.size());
        VK_CHECK(vkCreateComputePipelines(device.Get(), device.GetPipelineCache(), infos.size(), infos.data(), nullptr, pipes.data()));

        std::vector<std::unique_ptr<ComputePipeline>> res;
        res.reserve(pipes.size());
        for (const auto& pipe : pipes)
            res.emplace_back(std::make_unique<ComputePipeline>(device, pipe));
        return res;
    }
}