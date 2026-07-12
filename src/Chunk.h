#pragma once

#include "Block.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <raylib.h>

struct VertexCPU {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    float ao;
};

class TerrainGenerator;

class Chunk {
public:
    static constexpr int SIZE = 64;

    Chunk() = default;

    void generate(const TerrainGenerator& gen, int chunkX, int chunkY, int chunkZ);

    BlockType getBlock(int x, int y, int z) const;
    void      setBlock(int x, int y, int z, BlockType type);

    void buildMesh(std::function<BlockType(int, int, int)> blockQuery);
    void uploadToGPU();
    void unloadGPU();
    void clearBlocks();

    Mesh& getMesh() { return m_mesh; }
    bool  hasMesh() const { return m_hasMesh; }
    bool  hasBlocks() const { return m_blocks != nullptr; }

    int getChunkX() const { return m_chunkX; }
    int getChunkY() const { return m_chunkY; }
    int getChunkZ() const { return m_chunkZ; }

private:
    BlockType getBlockSafe(int x, int y, int z) const;
    void      addFace(int x, int y, int z, int face, const glm::vec3& color, const float ao[4]);
    float     calcVertexAO(int x, int y, int z, int face, int vertex);

    std::unique_ptr<std::array<BlockType, SIZE * SIZE * SIZE>> m_blocks;

    std::vector<VertexCPU> m_vertices;
    std::vector<uint32_t>  m_indices;

    Mesh m_mesh{};
    bool m_hasMesh = false;

    int m_chunkX = 0;
    int m_chunkY = 0;
    int m_chunkZ = 0;
    std::function<BlockType(int, int, int)> m_blockQuery;

    static const int FACE_VERTICES[6][4][3];
    static const uint32_t FACE_INDICES[6];
    static const int FACE_NORMALS[6][3];
};
