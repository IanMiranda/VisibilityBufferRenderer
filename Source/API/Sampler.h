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

		VkSampler& TrilinearColor() { return mTrilinearColor; }
		VkSampler& TrilinearColorClamp() { return mTrilinearColor; }
		VkSampler& NearestColor() { return mNearestColor; }
		VkSampler& Shadow() { return mShadow; }

	private:
		VkSamplerCreateInfo GetDefaultSamplerInfo() const;

	private:
		Device& mDevice;

		VkSampler mTrilinearColor;
		VkSampler mTrilinearColorClamp;
		VkSampler mNearestColor;
		VkSampler mShadow;
	};
}