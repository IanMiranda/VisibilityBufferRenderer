#pragma once

#include <deque>
#include <unordered_map>

#include "Common.h"
#include "DescriptorPool.h"
#include "DescriptorSetLayout.h"

namespace im
{
	class Device;
	class Texture2D;
	
	class BindlessSet
	{
	public:
		BindlessSet(Device& device, uint32_t maxTextures);
		~BindlessSet();

		DescriptorSetLayout& GetSetLayout() { return mBindlessSetLayout; }
		VkDescriptorSet Get() const	{ return mBindlessSet; }

		uint32_t GetOrCreateId(std::shared_ptr<Texture2D> texture, VkSampler sampler);

	private:
		Device& mDevice;
		const uint32_t mMaxTextures;

		DescriptorPool mBindlessPool;
		DescriptorSetLayout mBindlessSetLayout;
		VkDescriptorSet mBindlessSet;

		std::deque<uint32_t> mTexFreeList;
		std::unordered_map<std::shared_ptr<Texture2D>, uint32_t> mTexMap;
	};
}