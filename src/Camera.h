#pragma once

#include "Block.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class FPSCamera {
public:
    explicit FPSCamera(glm::vec3 position = glm::vec3(0.0f, 2.0f, 5.0f));

    void processKeyboard(float deltaTime);
    void processMouse(float xOffset, float yOffset);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;

    glm::vec3 getPosition()  const { return m_position; }
    glm::vec3 getFront()     const { return m_front; }
    float     getYaw()       const { return m_yaw; }
    float     getPitch()     const { return m_pitch; }

    void setPosition(glm::vec3 pos) { m_position = pos; updateVectors(); }

private:
    void updateVectors();

    glm::vec3 m_position;
    float m_yaw   = -90.0f;
    float m_pitch = 0.0f;

    float m_speed       = 4.0f;
    float m_sprintSpeed = 12.0f;
    float m_sensitivity = 0.1f;
    float m_fov         = 60.0f;
    float m_nearPlane   = 0.1f;
    float m_farPlane    = 200.0f;

    glm::vec3 m_front{0.0f, 0.0f, -1.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};

    static constexpr float PITCH_LIMIT = 89.0f;
};
