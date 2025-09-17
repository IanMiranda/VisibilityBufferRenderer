#include "Camera.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace im
{
	static constexpr glm::vec3 gUpVector(0.0f, 1.0f, 0.0f);

	Camera::Camera(const glm::vec3& position)
		: position(position)
	{
		Update();
	}

	void Camera::Update()
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
			glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

