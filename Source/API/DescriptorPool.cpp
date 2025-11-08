#include "DescriptorPool.h"

#include "Device.h"

namespace im
{
    DescriptorPool::DescriptorPool(
        Device& device,
        const std::vector<VkDescriptorPoolSize>& poolSizes,
        uint32_t maxSets,
        VkDescriptorPoolCreateFlags flags)
        : mDevice(device)
    {
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.flags = flags;
        poolInfo.maxSets = maxSets;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        VK_CHECK(vkCreateDescriptorPool(mDevice.Get(), &poolInfo, nullptr, &mPool));
    }

    DescriptorPool::~DescriptorPool()
    {
        mDevice.WaitIdle();
        vkDestroyDescriptorPool(mDevice.Get(), mPool, nullptr);
    }
}