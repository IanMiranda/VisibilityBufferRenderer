#include "Camera.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace im
{
	Camera::Camera(
		float fov, float aspectRatio,
		float nearDistance, float farDistance,
		const glm::vec3& position
	)
		: fov(fov)
		, aspectRatio(aspectRatio)
		, nearDistance(nearDistance)
		, farDistance(farDistance)
		, position(position)
		, rotation(1.0f, 0.0f, 0.0f, 0.0f)
	{
	}

	glm::mat4 Camera::GetViewMatrix() const
	{
		// The camera rotates the objects in the opposite direction, so take the conjugate
		return glm::mat4_cast(glm::conjugate(rotation)) * glm::translate(glm::mat4(1.0), -position);
	}

	glm::mat4 Camera::GetProjectionMatrix() const
	{
		return glm::perspective(
			glm::radians(fov),
			aspectRatio,
			nearDistance,
			farDistance
		);
	}
}

