#include "BindlessSet.h"

#include <cassert>
#include <numeric>

#include "API/DescriptorSetLayout.h"
#include "API/Device.h"
#include "API/ImageView.h"

namespace im
{
    BindlessSet::BindlessSet(Device &device, uint32_t maxTextures)
        : mDevice(device), mMaxTextures(maxTextures),
          mBindlessPool(mDevice,
                        std::initializer_list<VkDescriptorPoolSize>{
                            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, mMaxTextures},
                            {VK_DESCRIPTOR_TYPE_SAMPLER, 1}},
                        1, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT),
          mBindlessSetLayout(
              mDevice,
              {
                  DescriptorSetLayout::Binding(
                      0, VK_DESCRIPTOR_TYPE_SAMPLER,
                      VK_SHADER_STAGE_FRAGMENT_BIT |
                          VK_SHADER_STAGE_COMPUTE_BIT,
                      1, &mDevice.GetSamplers().TrilinearColor()),
                  DescriptorSetLayout::Binding(1,
                                               VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                               VK_SHADER_STAGE_FRAGMENT_BIT |
                                                   VK_SHADER_STAGE_COMPUTE_BIT,
                                               mMaxTextures),
              },
              {0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                      VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
                      VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT}),
          mTexFreeList(mMaxTextures)
    {
        VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
        variableDescInfo.descriptorSetCount = 1;
        variableDescInfo.pDescriptorCounts = &mMaxTextures;

        mBindlessSet =
            mBindlessPool.Allocate(mBindlessSetLayout, &variableDescInfo);

        std::iota(mTexFreeList.begin(), mTexFreeList.end(), 0);
    }

    uint32_t BindlessSet::GetOrCreateId(std::shared_ptr<Texture2D> texture)
    {
        if (const auto it = mTexMap.find(texture); it != mTexMap.end())
        {
            return it->second;
        }
        else
        {
            assert(!mTexFreeList.empty() && "No more textures left!");
            const auto idx = mTexFreeList.front();

            mBindlessSet
                ->PushWrite(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, *texture->view,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, idx)
                .Update();

            mTexMap[texture] = idx;
            mTexFreeList.pop_front();
            return idx;
        }
    }
} // namespace im