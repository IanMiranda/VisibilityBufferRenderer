#include "BindlessSet.h"

#include <cassert>

#include "Device.h"
#include "Texture2D.h"

namespace im
{
	BindlessSet::BindlessSet(Device& device, uint32_t maxTextures)
		: mDevice(device), mMaxTextures(maxTextures)
	{
		std::vector<VkDescriptorPoolSize> sizes =
		{
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mMaxTextures }
		};
		VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		poolInfo.maxSets = 1;
		poolInfo.poolSizeCount = sizes.size();
		poolInfo.pPoolSizes = sizes.data();

		VK_CHECK(vkCreateDescriptorPool(mDevice.Get(), &poolInfo, nullptr, &mBindlessPool));

		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		setLayoutBindings.reserve(1);

		VkDescriptorSetLayoutBinding textureBinding{};
		textureBinding.binding = 0;
		textureBinding.descriptorCount = mMaxTextures;
		textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		setLayoutBindings.emplace_back(textureBinding);

		VkDescriptorBindingFlags bindingFlags =
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
		
		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
		bindingFlagsInfo.bindingCount = 1;
		bindingFlagsInfo.pBindingFlags = &bindingFlags;

		VkDescriptorSetLayoutCreateInfo setLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		setLayoutInfo.pNext = &bindingFlagsInfo;
		setLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		setLayoutInfo.bindingCount = setLayoutBindings.size();
		setLayoutInfo.pBindings = setLayoutBindings.data();
		VK_CHECK(vkCreateDescriptorSetLayout(mDevice.Get(), &setLayoutInfo, nullptr, &mBindlessSetLayout));

		VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
		variableDescInfo.descriptorSetCount = 1;
		variableDescInfo.pDescriptorCounts = &mMaxTextures;

		VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		setInfo.pNext = &variableDescInfo;
		setInfo.descriptorPool = mBindlessPool;
		setInfo.descriptorSetCount = 1;
		setInfo.pSetLayouts = &mBindlessSetLayout;
		VK_CHECK(vkAllocateDescriptorSets(mDevice.Get(), &setInfo, &mBindlessSet));

		for (uint32_t i = 0; i < mMaxTextures; ++i)
			mTexFreeList.emplace_back(i);
	}

	BindlessSet::~BindlessSet()
	{
		mDevice.WaitIdle();
		vkDestroyDescriptorSetLayout(mDevice.Get(), mBindlessSetLayout, nullptr);
		vkDestroyDescriptorPool(mDevice.Get(), mBindlessPool, nullptr);
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