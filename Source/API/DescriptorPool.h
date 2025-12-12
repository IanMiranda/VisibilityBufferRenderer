#pragma once

#include "Common.h"

namespace im
{
    class Device;
    class DescriptorSetLayout;
    class DescriptorSet;

    class DescriptorPool
    {
    public:
        DescriptorPool(
            Device& device,
            std::initializer_list<VkDescriptorPoolSize> poolSizes,
            uint32_t maxSets,
            VkDescriptorPoolCreateFlags flags = 0);
        ~DescriptorPool();

        DescriptorPool(const DescriptorPool& other) = delete;
		DescriptorPool& operator=(const DescriptorPool& other) = delete;

        VkDescriptorPool Get() const { return mPool; }
        Device& GetDevice();

		std::unique_ptr<DescriptorSet> Allocate(DescriptorSetLayout& layout, void* pNext = nullptr);
        
		std::vector<std::unique_ptr<DescriptorSet>> Allocate(
            std::initializer_list<std::reference_wrapper<DescriptorSetLayout>> layouts, void* pNext = nullptr);

    private:
        Device& mDevice;

        VkDescriptorPool mPool;
    };
}