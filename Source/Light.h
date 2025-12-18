#pragma once

#include "Common.h"

namespace im
{
	inline constexpr uint32_t gMaxLights = 1024;

	struct PointLight
	{
		glm::vec3 position;
		float pad0;
		glm::vec4 i = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	};

	struct LightData
	{
		PointLight lights[gMaxLights];
	};
}