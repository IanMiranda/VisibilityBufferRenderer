#include "DescriptorSet.h"

#include "Device.h"
#include "DescriptorPool.h"
#include "Buffer.h"
#include "Texture2D.h"
#include "TextureCube.h"

namespace im
{
    DescriptorSet::DescriptorSet(
        DescriptorPool& pool,
        VkDescriptorSet set)
        : mPool(pool)
        , mSet(set)
    {
    }

    DescriptorSet::~DescriptorSet()
    {
        // TODO: support freeing individual descriptor sets?
    }

    DescriptorSet& DescriptorSet::PushWrite(
        uint32_t binding,
        VkDescriptorType type,
        const Buffer& buffer,
        uint32_t arrayIndex)
    {
        auto bufferInfo = std::make_unique<VkDescriptorBufferInfo>();
        bufferInfo->buffer = buffer.Get();
        bufferInfo->offset = 0;
        bufferInfo->range = buffer.GetSize();
        mBufferWrites.emplace_back(std::move(bufferInfo));

        auto write = MakeWrite(type, arrayIndex, binding);
        write.pBufferInfo = mBufferWrites.back().get();
        mWrites.emplace_back(write);
        return *this;
    }

    DescriptorSet& DescriptorSet::PushWrite(
        uint32_t binding,
        VkDescriptorType type,
        Texture2D* texture,
        VkSampler* sampler,
        VkImageLayout imageLayout,
        uint32_t arrayIndex)
    {
        auto imageInfo = std::make_unique<VkDescriptorImageInfo>();
        imageInfo->imageLayout = imageLayout;
        imageInfo->imageView = (texture ? texture->GetView() : VK_NULL_HANDLE);
        imageInfo->sampler = (sampler ? (*sampler) : VK_NULL_HANDLE);
        mImageWrites.emplace_back(std::move(imageInfo));

        auto write = MakeWrite(type, arrayIndex, binding);
        write.pImageInfo = mImageWrites.back().get();
        mWrites.emplace_back(write);
        return *this;
    }

    DescriptorSet& DescriptorSet::PushWrite(
        uint32_t binding,
        VkDescriptorType type,
        TextureCube* texture,
        VkSampler* sampler,
        VkImageLayout imageLayout,
        uint32_t arrayIndex)
    {
        auto imageInfo = std::make_unique<VkDescriptorImageInfo>();
        imageInfo->imageLayout = imageLayout;
        imageInfo->imageView = (texture ? texture->GetView() : VK_NULL_HANDLE);
        imageInfo->sampler = (sampler ? (*sampler) : VK_NULL_HANDLE);
        mImageWrites.emplace_back(std::move(imageInfo));

        auto write = MakeWrite(type, arrayIndex, binding);
        write.pImageInfo = mImageWrites.back().get();
        mWrites.emplace_back(write);
        return *this;
    }

    void DescriptorSet::Update()
    {
        vkUpdateDescriptorSets(mPool.GetDevice().Get(), mWrites.size(), mWrites.data(), 0, nullptr);
        mWrites.clear();
        mBufferWrites.clear();
        mImageWrites.clear();
    }

    VkWriteDescriptorSet DescriptorSet::MakeWrite(VkDescriptorType type, uint32_t arrayIndex, uint32_t binding) const
    {
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.dstArrayElement = arrayIndex;
        write.dstBinding = binding;
        write.dstSet = mSet;
        return write;
    }
}