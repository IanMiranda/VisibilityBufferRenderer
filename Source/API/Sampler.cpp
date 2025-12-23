#include "Sampler.h"

#include "Device.h"

namespace im
{
	Samplers::Samplers(Device& device) : mDevice(device)
	{
		{
			auto trilinearColorInfo = GetDefaultSamplerInfo();
			trilinearColorInfo.compareEnable = VK_FALSE;
			trilinearColorInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			VK_CHECK(vkCreateSampler(mDevice.Get(), &trilinearColorInfo, nullptr, &mTrilinearColor));
		}
		{
			auto trilinearColorInfo = GetDefaultSamplerInfo();
			trilinearColorInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			trilinearColorInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			trilinearColorInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			trilinearColorInfo.compareEnable = VK_FALSE;
			trilinearColorInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			VK_CHECK(vkCreateSampler(mDevice.Get(), &trilinearColorInfo, nullptr, &mTrilinearColorClamp));
		}
		{
			{
			auto nearestColor = GetDefaultSamplerInfo();
			nearestColor.minFilter = VK_FILTER_NEAREST;
			nearestColor.magFilter = VK_FILTER_NEAREST;
			nearestColor.compareEnable = VK_FALSE;
			nearestColor.compareOp = VK_COMPARE_OP_ALWAYS;
			VK_CHECK(vkCreateSampler(mDevice.Get(), &nearestColor, nullptr, &mNearestColor));
		}
		}

		{
			auto shadowInfo = GetDefaultSamplerInfo();
			shadowInfo.compareEnable = VK_TRUE;
			shadowInfo.compareOp = VK_COMPARE_OP_LESS;
			VK_CHECK(vkCreateSampler(mDevice.Get(), &shadowInfo, nullptr, &mShadow));
		}
	}

	Samplers::~Samplers()
	{
		mDevice.WaitIdle();
		vkDestroySampler(mDevice.Get(), mTrilinearColor, nullptr);
		vkDestroySampler(mDevice.Get(), mTrilinearColorClamp, nullptr);
		vkDestroySampler(mDevice.Get(), mNearestColor, nullptr);
		vkDestroySampler(mDevice.Get(), mShadow, nullptr);
	}

	VkSamplerCreateInfo Samplers::GetDefaultSamplerInfo() const
	{
		VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		return samplerInfo;
	}
}