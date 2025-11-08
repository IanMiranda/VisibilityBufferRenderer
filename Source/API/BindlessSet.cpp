#include "BindlessSet.h"

#include <cassert>

#include "Device.h"
#include "Texture2D.h"
#include "DescriptorSetLayout.h"

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

		const auto bindlessSetLayout = mBindlessSetLayout.Get();
		VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		setInfo.pNext = &variableDescInfo;
		setInfo.descriptorPool = mBindlessPool.Get();
		setInfo.descriptorSetCount = 1;
		setInfo.pSetLayouts = &bindlessSetLayout;
		VK_CHECK(vkAllocateDescriptorSets(mDevice.Get(), &setInfo, &mBindlessSet));

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

			VkDescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageInfo.imageView = texture->GetView();
			imageInfo.sampler = sampler;

			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.descriptorCount = 1;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.dstArrayElement = mTexFreeList.front();
			write.dstBinding = 0;
			write.dstSet = mBindlessSet;
			write.pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(mDevice.Get(), 1, &write, 0, nullptr);
			mTexFreeList.pop_front();

			mTexMap[texture] = write.dstArrayElement;
			return write.dstArrayElement;
		}
	}
}