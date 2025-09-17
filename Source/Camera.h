#pragma once

#include <glm/glm.hpp>

namespace im
{
	class Camera
	{
	public:
		glm::vec3 position;
		glm::vec3 front;

		float yaw{ 90.0f };
		float pitch{ 0.0f };

	public:
		Camera(const glm::vec3& position = glm::vec3(0.0f));

		void Update();

		glm::mat4 GetViewMatrix() const;
	};
}