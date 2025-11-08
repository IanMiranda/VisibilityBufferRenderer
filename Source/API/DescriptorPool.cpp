#include "DescriptorPool.h"

#include "Device.h"
#include "DescriptorSetLayout.h"
#include "DescriptorSet.h"

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

    Device& DescriptorPool::GetDevice()
    {
        return mDevice;
    }

    std::unique_ptr<DescriptorSet> DescriptorPool::Allocate(DescriptorSetLayout& layout, void* pNext)
    {
        return std::move(Allocate({ std::ref(layout) }, pNext).back());
    }

    std::vector<std::unique_ptr<DescriptorSet>> DescriptorPool::Allocate(
        const std::vector<std::reference_wrapper<DescriptorSetLayout>>& layouts,
        void* pNext)
    {
        std::vector<VkDescriptorSetLayout> vulkanLayouts;
        vulkanLayouts.reserve(layouts.size());
        for (const auto& layout : layouts)
            vulkanLayouts.emplace_back(layout.get().Get());
         
		VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.pNext = pNext;
		allocInfo.descriptorPool = mPool;
		allocInfo.descriptorSetCount = vulkanLayouts.size();
		allocInfo.pSetLayouts = vulkanLayouts.data();

        std::vector<VkDescriptorSet> descriptorSets(vulkanLayouts.size());
		VK_CHECK(vkAllocateDescriptorSets(mDevice.Get(), &allocInfo, descriptorSets.data()));

        std::vector<std::unique_ptr<DescriptorSet>> res;
        res.reserve(descriptorSets.size());
        for (auto set : descriptorSets)
            res.emplace_back(std::make_unique<DescriptorSet>(*this, set));
        return res;
    }
}