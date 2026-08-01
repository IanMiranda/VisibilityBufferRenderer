#pragma once

#include "Common.h"
#include "Device.h"
#include "PipelineLayout.h"
#include "Shader.h"

namespace im
{
    class ComputePipelineDesc
    {
        friend class ComputePipeline;

      public:
        ComputePipelineDesc(const PipelineLayout &layout, const Shader &shader);

      private:
        const PipelineLayout &mLayout;
        const Shader &mShader;
    };

    class ComputePipeline
    {
      public:
        ComputePipeline(Device &device, const ComputePipelineDesc &desc);
        ~ComputePipeline();

        static std::vector<std::unique_ptr<ComputePipeline>> Create(
            Device &device, const std::vector<ComputePipelineDesc> &descs);

        ComputePipeline(const ComputePipeline &other) = delete;
        ComputePipeline &operator=(const ComputePipeline &other) = delete;

        const VkPipeline &Get() const
        {
            return mPipeline;
        }

        ComputePipeline(Device &device, VkPipeline pipeline);

      private:
        Device &mDevice;

        VkPipeline mPipeline;
    };

} // namespace im