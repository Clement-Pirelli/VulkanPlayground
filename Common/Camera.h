#pragma once
#include <vec.h>
#include "MathUtils.h"
#include <mat.h>
#include "Timer.h"

struct Directions
{
    bool forwards : 1;
    bool backwards : 1;
    bool left : 1;
    bool right : 1;
};


class Camera
{
public:

    Camera(vec3 givenPosition, vec3 givenFront, vec3 givenUp) : position(givenPosition), front(givenFront), up(givenUp)
    {
    }
    Camera() = default;

    void onMouseMovement(vec2 offset)
    {
        offset *= mouseSensitivity;

        yaw += offset.x();
        pitch += offset.y();

        if (pitch > math::degToRad(89.0f))
            pitch = math::degToRad(89.0f);
        if (pitch < math::degToRad(-89.0f))
            pitch = math::degToRad(-89.0f);

        updateVectors();
    }

    void handleMovement(Time deltaTime, Directions directions)
    {
        const float deltaTimeMilliseconds = deltaTime.asSeconds();
        const float finalSpeed = deltaTimeMilliseconds * speed;

        const vec3 rightVelocity = right * finalSpeed;
        const vec3 frontVelocity = front * finalSpeed;
        if (directions.left)  position -= rightVelocity;
        if (directions.right) position += rightVelocity;
        if (directions.forwards)    position += frontVelocity;
        if (directions.backwards)  position -= frontVelocity;

        updateVectors();
    }

    mat4x4 calculateViewMatrix() const
    {
        return mat4x4::lookAt(
            {
            .eye = position,
            .target = position + front,
            .up = up
            });
    }

    vec3 position{};
    vec3 front{};
    vec3 right{};
    vec3 up{};
    float speed = .01f;
    float mouseSensitivity = .01f;

private:

    float pitch = .0f;
    float yaw = math::degToRad(-90.0f);

    void updateVectors()
    {
        front = vec3
        {
            cosf(yaw) * cosf(pitch),
            sinf(pitch),
            sinf(yaw) * cosf(pitch)
        }.normalized();
        right = vec3::cross(front, vec3(.0f, 1.0f, .0f)).normalized();
        up = vec3::cross(right, front).normalized();
    }
};

