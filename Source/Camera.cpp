#include "Camera.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace im
{
	static constexpr glm::vec3 gUpVector(0.0f, 1.0f, 0.0f);

	Camera::Camera(const glm::vec3& position)
		: mPosition(position), mFront(0.0f, -1.0f, 0.0f)
	{
	}

	void Camera::Update()
	{
		mPitch = std::clamp(mPitch, -89.9f, 89.9f);
		mYaw = std::fmodf(mYaw, 360.0f);

		// Recalculate front vector
		mFront.y = glm::sin(glm::radians(mPitch));
		mFront.x = glm::cos(glm::radians(mYaw)) * glm::cos(glm::radians(mPitch));
		mFront.z = glm::sin(glm::radians(mYaw)) * glm::cos(glm::radians(mPitch));
		mFront = glm::normalize(mFront);
	}

	glm::mat4 Camera::GetViewMatrix() const
	{
		return glm::lookAt(
			mPosition,
			mPosition + mFront,
			glm::vec3(0.0f, 1.0f, 0.0f));
	}

	void Camera::SetPosition(const glm::vec3& position)
	{
		mPosition = position;
	}

	void Camera::SetFront(const glm::vec3& front)
	{
		mFront = front;
	}
}

