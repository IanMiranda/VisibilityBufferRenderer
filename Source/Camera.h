#pragma once

#include <glm/glm.hpp>

namespace im
{
	class Camera
	{
	public:
		Camera(const glm::vec3& position = glm::vec3(0.0f));

		void Update();

		glm::mat4 GetViewMatrix() const;

		glm::vec3 GetPosition() const { return mPosition; }
		void SetPosition(const glm::vec3& position);

		glm::vec3 GetFront() const { return mFront; }
		void SetFront(const glm::vec3& front);

		float GetYaw() const { return mYaw; }
		void SetYaw(float yaw) { mYaw = yaw; }

		float GetPitch() const { return mPitch; }
		void SetPitch(float pitch) { mPitch = pitch; }

	private:
		glm::vec3 mPosition;
		glm::vec3 mFront;

		float mYaw{ 90.0f };
		float mPitch{ 0.0f };
	};
}