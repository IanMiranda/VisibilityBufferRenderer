#pragma once

#include "Common.h"

namespace im
{
    class Device;

    class DescriptorPool
    {
    public:
        DescriptorPool(
            Device& device,
            const std::vector<VkDescriptorPoolSize>& poolSizes,
            uint32_t maxSets,
            VkDescriptorPoolCreateFlags flags = 0);
        ~DescriptorPool();

        DescriptorPool(const DescriptorPool& other) = delete;
		DescriptorPool& operator=(const DescriptorPool& other) = delete;

        VkDescriptorPool Get() const { return mPool; }

    private:
        Device& mDevice;

        VkDescriptorPool mPool;
    };
}