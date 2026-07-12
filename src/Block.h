#pragma once

#include <cstdint>
#include <string_view>
#include <glm/glm.hpp>

enum class BlockType : uint8_t {
    Air   = 0,
    Grass = 1,
    Dirt  = 2,
    Stone = 3,
    Sand  = 4,
    Wood  = 5,
    Water = 6,
    Snow  = 7,
    Bedrock = 8,
    Gravel = 9,
    Clay  = 10,
    CoalOre = 11,
    IronOre = 12,
    GoldOre = 13,
    DiamondOre = 14,
    OakPlank = 15,
    Brick = 16,
    COUNT
};

struct BlockInfo {
    glm::vec3 color;
    bool      solid;
};

//  To add a new block:
//  1. Add an entry to the BlockType enum above (before COUNT)
//  2. Add a matching row here: { color, solid }
//
inline constexpr BlockInfo BLOCK_REGISTRY[] = {
//  Color                   Solid
    {{ 1.00f, 1.00f, 1.00f}, false},  // Air
    {{ 0.36f, 0.65f, 0.28f}, true },  // Grass
    {{ 0.55f, 0.37f, 0.24f}, true },  // Dirt
    {{ 0.50f, 0.50f, 0.50f}, true },  // Stone
    {{ 0.85f, 0.80f, 0.55f}, true },  // Sand
    {{ 0.55f, 0.35f, 0.15f}, true },  // Wood
    {{ 0.20f, 0.40f, 0.80f}, false},  // Water
    {{ 0.95f, 0.95f, 0.97f}, true },  // Snow
    {{ 0.20f, 0.20f, 0.20f}, true },  // Bedrock
    {{ 0.60f, 0.55f, 0.50f}, true },  // Gravel
    {{ 0.75f, 0.60f, 0.50f}, true },  // Clay
    {{ 0.25f, 0.25f, 0.25f}, true },  // CoalOre
    {{ 0.72f, 0.55f, 0.40f}, true },  // IronOre
    {{ 0.85f, 0.75f, 0.25f}, true },  // GoldOre
    {{ 0.30f, 0.80f, 0.85f}, true },  // DiamondOre
    {{ 0.72f, 0.56f, 0.32f}, true },  // OakPlank
    {{ 0.65f, 0.30f, 0.25f}, true },  // Brick
};

static_assert(static_cast<uint8_t>(BlockType::COUNT) == sizeof(BLOCK_REGISTRY) / sizeof(BLOCK_REGISTRY[0]),
    "BLOCK_REGISTRY size does not match BlockType::COUNT");

inline const BlockInfo& getBlockInfo(BlockType type) {
    return BLOCK_REGISTRY[static_cast<uint8_t>(type)];
}

inline glm::vec3 getBlockColor(BlockType type) {
    return getBlockInfo(type).color;
}

inline bool isBlockSolid(BlockType type) {
    return getBlockInfo(type).solid;
}

constexpr float BLOCK_SIZE = 0.25f;
