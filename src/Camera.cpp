#include "Camera.h"

#include <raylib.h>

#include <cmath>

FPSCamera::FPSCamera(glm::vec3 position) : m_position(position) {
    updateVectors();
}

void FPSCamera::processKeyboard(float deltaTime) {
    float velocity = IsKeyDown(KEY_LEFT_SHIFT) ? m_sprintSpeed : m_speed;
    velocity *= deltaTime;

    if (IsKeyDown(KEY_W)) m_position += m_front * velocity;
    if (IsKeyDown(KEY_S)) m_position -= m_front * velocity;
    if (IsKeyDown(KEY_A)) m_position -= m_right * velocity;
    if (IsKeyDown(KEY_D)) m_position += m_right * velocity;
    if (IsKeyDown(KEY_SPACE))          m_position += m_up * velocity;
    if (IsKeyDown(KEY_LEFT_CONTROL))   m_position -= m_up * velocity;
}

void FPSCamera::processMouse(float xOffset, float yOffset) {
    xOffset *= m_sensitivity;
    yOffset *= m_sensitivity;

    m_yaw   += xOffset;
    m_pitch += yOffset;

    if (m_pitch >  PITCH_LIMIT) m_pitch =  PITCH_LIMIT;
    if (m_pitch < -PITCH_LIMIT) m_pitch = -PITCH_LIMIT;

    updateVectors();
}

glm::mat4 FPSCamera::getViewMatrix() const {
    return glm::lookAt(m_position, m_position + m_front, m_up);
}

glm::mat4 FPSCamera::getProjectionMatrix(float aspectRatio) const {
    return glm::perspective(
        glm::radians(m_fov), aspectRatio, m_nearPlane, m_farPlane);
}

void FPSCamera::updateVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    m_front = glm::normalize(front);

    m_right = glm::normalize(glm::cross(m_front, glm::vec3(0.0f, 1.0f, 0.0f)));
    m_up    = glm::normalize(glm::cross(m_right, m_front));
}
