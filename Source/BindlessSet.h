#pragma once

#include <deque>
#include <unordered_map>

#include "Common.h"
#include "API/DescriptorPool.h"
#include "API/DescriptorSetLayout.h"
#include "API/DescriptorSet.h"

namespace im
{
	class Device;
	struct Texture2D;
	
	class BindlessSet
	{
	public:
		static constexpr uint32_t MaxTextures{ 512 };

	public:
		BindlessSet(Device& device, uint32_t maxTextures = MaxTextures);
		~BindlessSet();

		DescriptorSetLayout& GetSetLayout() { return mBindlessSetLayout; }
		DescriptorSet& Get() { return *mBindlessSet; }

		uint32_t GetOrCreateId(std::shared_ptr<Texture2D> texture);

	private:
		Device& mDevice;
		const uint32_t mMaxTextures;

		DescriptorPool mBindlessPool;
		DescriptorSetLayout mBindlessSetLayout;
		std::unique_ptr<DescriptorSet> mBindlessSet;

		std::deque<uint32_t> mTexFreeList;
		std::unordered_map<std::shared_ptr<Texture2D>, uint32_t> mTexMap;
	};
}