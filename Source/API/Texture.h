#pragma once

#include "Common.h"

namespace im
{
	class Texture
	{
	public:
		virtual ~Texture() = default;
	
		virtual VkImageView GetView() const = 0;

	protected:
		Texture() = default;
	};
}