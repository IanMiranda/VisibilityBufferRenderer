#pragma once

#include "Common.h"

namespace im
{
	class Device;

	class Samplers
	{
	public:
		Samplers(Device& device);
		~Samplers();

		Samplers(const Samplers& other) = delete;
		Samplers& operator=(const Samplers& other) = delete;

		VkSampler TrilinearColor() const { return mTrilinearColor; }
		VkSampler Shadow() const { return mShadow; }

	private:
		VkSamplerCreateInfo GetDefaultSamplerInfo() const;

	private:
		Device& mDevice;

		VkSampler mTrilinearColor;
		VkSampler mShadow;
	};
}