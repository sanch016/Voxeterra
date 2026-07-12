#pragma once

#include "Block.h"
#include <glm/glm.hpp>

class World;

class Player {
public:
    explicit Player(glm::vec3 spawnPos = glm::vec3(16.0f, 25.0f, 16.0f));

    void processInput(float deltaTime, float cameraYaw);
    void update(float deltaTime, const World& world);

    glm::vec3 getPosition()    const { return m_position; }
    glm::vec3 getEyePosition() const { return m_position + glm::vec3(0.0f, m_eyeHeight, 0.0f); }
    bool      isGrounded()     const { return m_grounded; }

    struct AABB {
        glm::vec3 min;
        glm::vec3 max;
    };

    AABB getAABB() const;

private:
    void resolveCollisionsAxis(const World& world, int axis);

    glm::vec3 m_position;
    glm::vec3 m_velocity{0.0f};

    float m_width       = 0.6f * BLOCK_SIZE;
    float m_height      = 1.8f * BLOCK_SIZE;
    float m_eyeHeight   = 1.62f * BLOCK_SIZE;

    float m_moveSpeed    = 4.5f * BLOCK_SIZE;
    float m_sprintSpeed  = 10.0f * BLOCK_SIZE;
    float m_jumpVelocity = 7.5f * BLOCK_SIZE;
    float m_gravity      = 22.0f * BLOCK_SIZE;

    bool m_grounded = false;
};
