#pragma once

#include "Common.h"
#include "API/DescriptorSetLayout.h"
#include "API/DescriptorPool.h"
#include "API/DescriptorSet.h"

namespace im
{
	class Device;

	class DescriptorSetAllocator
	{
	private:
		constexpr static uint32_t SetsPerPool{ 500 };
		constexpr static uint32_t UniformBuffersPerSet{ 8 };
		constexpr static uint32_t CombinedImageSamplersPerSet{ 4 };
		constexpr static uint32_t StorageImagesPerSet{ 4 };
		constexpr static uint32_t SampledImagesPerSet{ 6 };
		constexpr static uint32_t SamplersPerSet{ 1 };

	public:
		DescriptorSetAllocator(Device& device);
		~DescriptorSetAllocator();

		DescriptorSetAllocator(const DescriptorSetAllocator& other) = delete;
		DescriptorSetAllocator& operator=(const DescriptorSetAllocator& other) = delete;

		std::vector<std::unique_ptr<DescriptorSet>> Allocate(
			std::initializer_list<std::reference_wrapper<DescriptorSetLayout>> setLayouts
		);
		std::unique_ptr<DescriptorSet> Allocate(DescriptorSetLayout& setLayout);

	private:
		struct PoolInfo
		{
			std::unique_ptr<DescriptorPool> pool;
			std::unordered_map<VkDescriptorType, uint32_t> remainingBindings;
			uint32_t remainingSets;
		};

		PoolInfo& FindFirstPool(std::unordered_map<VkDescriptorType, uint32_t>& requirements);

		PoolInfo CreatePool();

	private:
		Device& mDevice;

		std::vector<PoolInfo> mPools;
	};
}