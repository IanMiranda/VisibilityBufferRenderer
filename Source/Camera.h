#pragma once

#include <glm/glm.hpp>

namespace im
{
	class Camera
	{
	public:
		Camera(const glm::vec3& position = glm::vec3(0.0f));

		glm::mat4 GetViewMatrix() const;

	private:
		glm::vec3 mPosition;
	};
}