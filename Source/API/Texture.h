#pragma once

#include "Common.h"

namespace im
{
	class Texture
	{
	public:
		virtual ~Texture() = default;
	
		virtual VkImageView GetView() const = 0;

		static uint32_t GetMaxMipLevels(uint32_t width, uint32_t height)
		{
			return std::floor(std::log2(std::max(width, height))) + 1;
		}

	protected:
		Texture() = default;
	};
}