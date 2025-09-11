#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>

namespace im
{
	Camera::Camera(const glm::vec3& position)
		: mPosition(position)
	{
	}

	glm::mat4 Camera::GetViewMatrix() const
	{
		return glm::lookAt(
			mPosition,
			mPosition + glm::vec3(0.0f, 0.0f, -1.0f),
			glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

