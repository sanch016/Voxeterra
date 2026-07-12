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
    static constexpr int SIZE = 48;
    static constexpr int NUM_LODS = 4;

    Chunk() = default;
    ~Chunk() { unloadGPU(); }

    int  getLodLevel() const { return m_lodLevel; }
    int  getStep() const { return m_step; }

    void generate(const TerrainGenerator& gen, int chunkX, int chunkY, int chunkZ, int lodLevel = 0);

    void setLodLevel(int level) {
        m_lodLevel = level;
        m_step = 1 << level;
        if (m_step < 1) m_step = 1;
    }

    BlockType getBlock(int x, int y, int z) const;
    void      setBlock(int x, int y, int z, BlockType type);

    void buildMesh(std::function<BlockType(int, int, int)> blockQuery);
    void uploadToGPU();
    void unloadGPU();
    void clearBlocks();

    Mesh& getMesh() { return m_mesh; }
    bool hasMesh() const { return m_hasMesh; }
    bool hasBlocks() const { return m_blocks != nullptr; }
    bool isEmpty() const { return m_nonAirCount == 0; }
    bool isFullySolid() const {
        if (m_step == 1) return m_nonAirCount == SIZE * SIZE * SIZE;
        int perDim = SIZE / m_step;
        return m_nonAirCount == perDim * perDim * perDim;
    }

    std::shared_ptr<std::array<BlockType, SIZE * SIZE * SIZE>> shareBlocks() const {
        return m_blocks;
    }

    std::vector<VertexCPU> takeVertices() { return std::move(m_vertices); }
    std::vector<uint32_t>  takeIndices() { return std::move(m_indices); }
    void uploadFromData(std::vector<VertexCPU> verts, std::vector<uint32_t> indices);

    int getChunkX() const { return m_chunkX; }
    int getChunkY() const { return m_chunkY; }
    int getChunkZ() const { return m_chunkZ; }

    void setPosition(int x, int y, int z) { m_chunkX = x; m_chunkY = y; m_chunkZ = z; }
    void assignBlocks(const std::shared_ptr<std::array<BlockType, SIZE * SIZE * SIZE>>& src) {
        m_blocks = std::make_shared<std::array<BlockType, SIZE * SIZE * SIZE>>(*src);
    }

private:
    BlockType getBlockSafe(int x, int y, int z) const;
    void      addFace(int x, int y, int z, int face, const glm::vec3& color, const float ao[4]);
    float     calcVertexAO(int x, int y, int z, int face, int vertex);

    std::shared_ptr<std::array<BlockType, SIZE * SIZE * SIZE>> m_blocks;

    std::vector<VertexCPU> m_vertices;
    std::vector<uint32_t>  m_indices;

    Mesh m_mesh{};
    bool m_hasMesh = false;

    int m_chunkX = 0;
    int m_chunkY = 0;
    int m_chunkZ = 0;
    int m_lodLevel = 0;
    int m_step = 1;
    int m_nonAirCount = 0;
    std::function<BlockType(int, int, int)> m_blockQuery;

    static const int FACE_VERTICES[6][4][3];
    static const uint32_t FACE_INDICES[6];
    static const int FACE_NORMALS[6][3];
};
