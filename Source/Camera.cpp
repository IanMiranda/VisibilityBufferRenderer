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
	{
		UpdateFrontVector();
	}

	void Camera::UpdateFrontVector()
	{
		pitch = std::clamp(pitch, -89.9f, 89.9f);
		yaw = std::fmodf(yaw, 360.0f);

		// Recalculate front vector
		front.y = glm::sin(glm::radians(pitch));
		front.x = glm::cos(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
		front.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
		front = glm::normalize(front);
	}

	glm::mat4 Camera::GetViewMatrix() const
	{
		return glm::lookAt(
			position,
			position + front,
			WorldUpVector);
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

