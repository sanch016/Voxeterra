#include "Player.h"
#include "World.h"
#include "Block.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

Player::Player(glm::vec3 spawnPos) : m_position(spawnPos) {}

Player::AABB Player::getAABB() const {
    float halfW = m_width * 0.5f;
    return {
        m_position - glm::vec3(halfW, 0.0f, halfW),
        m_position + glm::vec3(halfW, m_height, halfW)
    };
}

void Player::processInput(float deltaTime, float cameraYaw) {
    float yawRad = glm::radians(cameraYaw);
    glm::vec3 front(cos(yawRad), 0.0f, sin(yawRad));
    front = glm::normalize(front);
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 moveDir(0.0f);
    if (IsKeyDown(KEY_W)) moveDir += front;
    if (IsKeyDown(KEY_S)) moveDir -= front;
    if (IsKeyDown(KEY_A)) moveDir -= right;
    if (IsKeyDown(KEY_D)) moveDir += right;

    if (IsKeyDown(KEY_SPACE))          moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
    if (IsKeyDown(KEY_LEFT_CONTROL))   moveDir += glm::vec3(0.0f, -1.0f, 0.0f);

    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
    }

    float speed = IsKeyDown(KEY_LEFT_SHIFT) ? m_sprintSpeed : m_moveSpeed;

    m_velocity = moveDir * speed;
}

static bool hasCollision(const World& world, const Player::AABB& box) {
    int minX = static_cast<int>(std::floor(box.min.x / BLOCK_SIZE));
    int maxX = static_cast<int>(std::floor(box.max.x / BLOCK_SIZE));
    int minY = static_cast<int>(std::floor(box.min.y / BLOCK_SIZE));
    int maxY = static_cast<int>(std::floor(box.max.y / BLOCK_SIZE));
    int minZ = static_cast<int>(std::floor(box.min.z / BLOCK_SIZE));
    int maxZ = static_cast<int>(std::floor(box.max.z / BLOCK_SIZE));

    for (int x = minX; x <= maxX; ++x)
        for (int y = minY; y <= maxY; ++y)
            for (int z = minZ; z <= maxZ; ++z) {
                BlockType b = world.getBlock(x, y, z);
                if (!isBlockSolid(b)) continue;
                float bx = x * BLOCK_SIZE, by = y * BLOCK_SIZE, bz = z * BLOCK_SIZE;
                float bs = BLOCK_SIZE;
                Player::AABB v = {{bx, by, bz}, {bx + bs, by + bs, bz + bs}};
                if (box.min.x < v.max.x && box.max.x > v.min.x &&
                    box.min.y < v.max.y && box.max.y > v.min.y &&
                    box.min.z < v.max.z && box.max.z > v.min.z)
                    return true;
            }
    return false;
}

void Player::update(float deltaTime, const World& world) {
    if (deltaTime > 0.05f) deltaTime = 0.05f;

    m_position += m_velocity * deltaTime;
}

void Player::resolveCollisionsAxis(const World& world, int axis) {
    AABB box = getAABB();

    int minX = static_cast<int>(std::floor(box.min.x / BLOCK_SIZE));
    int maxX = static_cast<int>(std::floor(box.max.x / BLOCK_SIZE));
    int minY = static_cast<int>(std::floor(box.min.y / BLOCK_SIZE));
    int maxY = static_cast<int>(std::floor(box.max.y / BLOCK_SIZE));
    int minZ = static_cast<int>(std::floor(box.min.z / BLOCK_SIZE));
    int maxZ = static_cast<int>(std::floor(box.max.z / BLOCK_SIZE));

    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                BlockType block = world.getBlock(x, y, z);
                if (!isBlockSolid(block)) continue;

                float bx = x * BLOCK_SIZE, by = y * BLOCK_SIZE, bz = z * BLOCK_SIZE;
                float bs = BLOCK_SIZE;
                AABB voxel = {
                    glm::vec3(bx, by, bz),
                    glm::vec3(bx + bs, by + bs, bz + bs)
                };

                if (box.min.x >= voxel.max.x || box.max.x <= voxel.min.x) continue;
                if (box.min.y >= voxel.max.y || box.max.y <= voxel.min.y) continue;
                if (box.min.z >= voxel.max.z || box.max.z <= voxel.min.z) continue;

                float halfW = m_width * 0.5f;

                if (axis == 0) {
                    if (m_velocity.x > 0.0f)
                        m_position.x = voxel.min.x - halfW;
                    else if (m_velocity.x < 0.0f)
                        m_position.x = voxel.max.x + halfW;
                    m_velocity.x = 0.0f;
                } else if (axis == 1) {
                    if (m_velocity.y < 0.0f) {
                        m_position.y = voxel.max.y;
                        m_grounded = true;
                    } else if (m_velocity.y > 0.0f) {
                        m_position.y = voxel.min.y - m_height;
                    }
                    m_velocity.y = 0.0f;
                } else {
                    if (m_velocity.z > 0.0f)
                        m_position.z = voxel.min.z - halfW;
                    else if (m_velocity.z < 0.0f)
                        m_position.z = voxel.max.z + halfW;
                    m_velocity.z = 0.0f;
                }

                box = getAABB();
            }
        }
    }
}
