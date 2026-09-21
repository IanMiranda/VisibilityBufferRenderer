#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace im
{
    class Camera
    {
    public:
        static constexpr glm::vec3 WorldUpVector{0.0f, 1.0f, 0.0f};

        float fov;
        float aspectRatio;
        float nearDistance;
        float farDistance;

        glm::vec3 position;
        glm::quat rotation;

    public:
        Camera(float fov, float aspectRatio, float nearDistance,
               float farDistance,
               const glm::vec3 &position = glm::vec3(0.0f, 0.5f, 0.0f));

        glm::vec3 GetFrontVector() const
        {
            return rotation * glm::vec3{0.0f, 0.0f, -1.0f};
        }
        glm::vec3 GetRightVector() const
        {
            return rotation * glm::vec3{1.0f, 0.0f, 0.0f};
        }

        glm::mat4 GetViewMatrix() const;
        glm::mat4 GetProjectionMatrix() const;
    };
} // namespace im