#include "DescriptorSetAllocator.h"

namespace im
{
	DescriptorSetAllocator::DescriptorSetAllocator(Device& device) : mDevice(device)
	{
	}

	DescriptorSetAllocator::~DescriptorSetAllocator()
	{
	}

	std::vector<std::unique_ptr<DescriptorSet>> DescriptorSetAllocator::Allocate(
		std::initializer_list<std::reference_wrapper<DescriptorSetLayout>> setLayouts
	)
	{
		std::unordered_map<VkDescriptorType, uint32_t> resourceRequirements;
		for (const auto& layout : setLayouts)
		{
			for (const auto& [resType, resCount] : layout.get().GetBindings())
			{
				resourceRequirements[resType] += resCount;
			}
		}

		auto& pool = FindFirstPool(resourceRequirements);
		for (const auto& [type, count] : resourceRequirements)
		{
			pool.remainingBindings[type] -= count;
		}
		pool.remainingSets -= 1;
		return pool.pool->Allocate(setLayouts);
	}

	std::unique_ptr<DescriptorSet> DescriptorSetAllocator::Allocate(DescriptorSetLayout& setLayout)
	{
		return std::move(Allocate(std::initializer_list{ std::ref(setLayout) }).back());
	}
	
	DescriptorSetAllocator::PoolInfo& DescriptorSetAllocator::FindFirstPool(
		std::unordered_map<VkDescriptorType, uint32_t>& requirements
	)
	{
		for (auto& pool : mPools)
		{
			if (pool.remainingSets == 0) continue;
			for (const auto& binding : pool.remainingBindings)
			{
				if (binding.second < requirements[binding.first]) continue;
			}
			return pool;
		}

		mPools.push_back(CreatePool());
		return mPools.back();
	}

	DescriptorSetAllocator::PoolInfo DescriptorSetAllocator::CreatePool()
	{
		PoolInfo res{};
		res.pool = std::make_unique<DescriptorPool>(
			mDevice,
			std::initializer_list<VkDescriptorPoolSize>{
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, CombinedImageSamplersPerSet * SetsPerPool },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffersPerSet * SetsPerPool },
				{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ImagesPerSet * SetsPerPool },
				{ VK_DESCRIPTOR_TYPE_SAMPLER, SamplersPerSet * SetsPerPool }
			},
			SetsPerPool
		);
		res.remainingBindings = {
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, CombinedImageSamplersPerSet * SetsPerPool },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, UniformBuffersPerSet * SetsPerPool },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ImagesPerSet * SetsPerPool },
			{ VK_DESCRIPTOR_TYPE_SAMPLER, SamplersPerSet * SetsPerPool }
		};
		res.remainingSets = SetsPerPool;
		return res;
	}
}