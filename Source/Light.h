#pragma once

#include "Common.h"

namespace im
{
	struct DirectionalLight
	{
		glm::vec3 direction;
		float _pad0;
	};

	struct PointLight
	{
		glm::vec3 position;
		float _pad0;
	};
}