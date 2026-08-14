#pragma once

#include <span>

#include "Common.h"

namespace im
{
    class Device;

    class DescriptorSetLayout
    {
    public:
        DescriptorSetLayout(
            Device &device,
            std::initializer_list<VkDescriptorSetLayoutBinding> bindings,
            VkDescriptorSetLayoutCreateFlags flags = 0);
        DescriptorSetLayout(
            Device &device,
            std::initializer_list<VkDescriptorSetLayoutBinding> bindings,
            std::initializer_list<VkDescriptorBindingFlags> flags);
        ~DescriptorSetLayout();

        DescriptorSetLayout(const DescriptorSetLayout &other) = delete;
        DescriptorSetLayout &operator=(const DescriptorSetLayout &other) =
            delete;

        static VkDescriptorSetLayoutBinding Binding(
            uint32_t index, VkDescriptorType type, VkShaderStageFlags stages,
            uint32_t count = 1, VkSampler *immutableSampler = nullptr);

        VkDescriptorSetLayout Get() const
        {
            return mLayout;
        }

        const std::unordered_map<VkDescriptorType, uint32_t> &GetBindings()
            const
        {
            return mBindingMap;
        }

    private:
        Device &mDevice;

        VkDescriptorSetLayout mLayout{VK_NULL_HANDLE};

        std::unordered_map<VkDescriptorType, uint32_t> mBindingMap;
    };
} // namespace im