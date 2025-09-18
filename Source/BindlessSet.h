#pragma once

#include <deque>

#include "Common.h"

namespace im
{
	class Device;
	class Texture2D;
	
	class BindlessSet
	{
	public:
		BindlessSet(Device& device, uint32_t maxTextures);
		~BindlessSet();

		VkDescriptorSetLayout GetSetLayout() const	{ return mBindlessSetLayout; }
		VkDescriptorSet Get() const	{ return mBindlessSet; }

		void RegisterTexture(VkDescriptorImageInfo imageInfo);

	private:
		Device& mDevice;
		const uint32_t mMaxTextures;

		VkDescriptorPool mBindlessPool;
		VkDescriptorSetLayout mBindlessSetLayout;
		VkDescriptorSet mBindlessSet;

		std::deque<uint32_t> mTexFreeList;
	};
}