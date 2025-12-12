#pragma once

#include "Common.h"

namespace im
{
    class DescriptorPool;
    class Buffer;
    class Texture2D;
    class TextureCube;

    class DescriptorSet
    {
    public:
        DescriptorSet(DescriptorPool& pool, VkDescriptorSet set);
        ~DescriptorSet();

        DescriptorSet(const DescriptorSet& other) = delete;
        DescriptorSet& operator=(const DescriptorSet& other) = delete;

        DescriptorSet& PushWrite(
            uint32_t binding,
            VkDescriptorType type,
            const Buffer& buffer,
            uint32_t arrayIndex = 0
        );

        DescriptorSet& PushWrite(
            uint32_t binding,
            VkDescriptorType type,
            Texture2D* texture = nullptr,
            VkSampler* sampler = nullptr,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            uint32_t arrayIndex = 0
        );

        DescriptorSet& PushWrite(
            uint32_t binding,
            VkDescriptorType type,
            TextureCube* texture = nullptr,
            VkSampler* sampler = nullptr,
            VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            uint32_t arrayIndex = 0
        );
        
        void Update();

        VkDescriptorSet Get() const { return mSet; }

    private:
        VkWriteDescriptorSet MakeWrite(VkDescriptorType type, uint32_t arrayIndex, uint32_t binding) const;

    private:
        DescriptorPool& mPool;
        VkDescriptorSet mSet;

        std::vector<std::unique_ptr<VkDescriptorBufferInfo>> mBufferWrites;
        std::vector<std::unique_ptr<VkDescriptorImageInfo>> mImageWrites;
        std::vector<VkWriteDescriptorSet> mWrites;
    };
}