#include "BindlessSet.h"

#include <cassert>

#include "API/Device.h"
#include "API/Texture2D.h"
#include "API/DescriptorSetLayout.h"

namespace im
{
	BindlessSet::BindlessSet(Device& device, uint32_t maxTextures)
		: mDevice(device)
		, mMaxTextures(maxTextures)
		, mBindlessPool(
			mDevice,
			std::vector<VkDescriptorPoolSize>{ { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mMaxTextures } },
			1, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
		)
		, mBindlessSetLayout(
			mDevice,
			std::vector<VkDescriptorSetLayoutBinding>{
				DescriptorSetLayout::Binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, mMaxTextures)
			},
			std::vector<VkDescriptorBindingFlags>{
				VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
				VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
				VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
				VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
			}
		)
	{
		VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
		variableDescInfo.descriptorSetCount = 1;
		variableDescInfo.pDescriptorCounts = &mMaxTextures;

		mBindlessSet = mBindlessPool.Allocate({ mBindlessSetLayout }, &variableDescInfo);
		for (uint32_t i = 0; i < mMaxTextures; ++i)
			mTexFreeList.emplace_back(i);
	}

	BindlessSet::~BindlessSet()
	{
	}

	uint32_t BindlessSet::GetOrCreateId(std::shared_ptr<Texture2D> texture, VkSampler sampler)
	{
		if (mTexMap.find(texture) != mTexMap.end())
		{
			return mTexMap[texture];
		}
		else
		{
			assert(!mTexFreeList.empty() && "No more textures left!");
			auto idx = mTexFreeList.front();

			mBindlessSet->PushWrite(
				0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				texture.get(), &sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				idx).Update();
			
			mTexMap[texture] = idx;
			mTexFreeList.pop_front();
			return idx;
		}
	}
}