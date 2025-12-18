#pragma once

#include <glm/glm.hpp>

namespace im
{
	class Camera
	{
	public:
		static constexpr glm::vec3 WorldUpVector{ 0.0f, 1.0f, 0.0f };

		float yaw{ 90.0f };
		float pitch{ 0.0f };

		float fov;
		float aspectRatio;
		float nearDistance;
		float farDistance;

		glm::vec3 position;
		glm::vec3 front;

	public:
		Camera(
			float fov, float aspectRatio,
			float nearDistance, float farDistance,
			const glm::vec3& position = glm::vec3(0.0f)
		);

		void UpdateFrontVector();

		glm::mat4 GetViewMatrix() const;
		glm::mat4 GetProjectionMatrix() const;
	};
}