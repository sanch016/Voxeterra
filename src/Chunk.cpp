#include "Chunk.h"
#include "TerrainGenerator.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

#include <FastNoiseLite.h>

// ── Static face data ──────────────────────────────────────────

const int Chunk::FACE_VERTICES[6][4][3] = {
    {{1,0,0}, {1,1,0}, {1,1,1}, {1,0,1}},
    {{0,0,1}, {0,1,1}, {0,1,0}, {0,0,0}},
    {{0,1,1}, {1,1,1}, {1,1,0}, {0,1,0}},
    {{0,0,0}, {1,0,0}, {1,0,1}, {0,0,1}},
    {{1,0,1}, {1,1,1}, {0,1,1}, {0,0,1}},
    {{0,0,0}, {0,1,0}, {1,1,0}, {1,0,0}}
};

const uint32_t Chunk::FACE_INDICES[6] = {0, 1, 2, 2, 3, 0};

const int Chunk::FACE_NORMALS[6][3] = {
    { 1,  0,  0},
    {-1,  0,  0},
    { 0,  1,  0},
    { 0, -1,  0},
    { 0,  0,  1},
    { 0,  0, -1}
};

static const int FACE_TANGENT1[6][3] = {
    {0, 1, 0}, {0, 1, 0},
    {1, 0, 0}, {1, 0, 0},
    {1, 0, 0}, {1, 0, 0}
};
static const int FACE_TANGENT2[6][3] = {
    {0, 0, 1}, {0, 0, 1},
    {0, 0, 1}, {0, 0, 1},
    {0, 1, 0}, {0, 1, 0}
};

static const int FACE_VERTEX_UV[6][4][2] = {
    {{0,0},{1,0},{1,1},{0,1}},
    {{0,1},{1,1},{1,0},{0,0}},
    {{0,1},{1,1},{1,0},{0,0}},
    {{0,0},{1,0},{1,1},{0,1}},
    {{1,0},{1,1},{0,1},{0,0}},
    {{0,0},{0,1},{1,1},{1,0}}
};

// ── Block access ──────────────────────────────────────────────

BlockType Chunk::getBlock(int x, int y, int z) const {
    if (!m_blocks) return BlockType::Air;
    return (*m_blocks)[x + y * SIZE + z * SIZE * SIZE];
}

void Chunk::setBlock(int x, int y, int z, BlockType type) {
    if (!m_blocks) m_blocks = std::make_shared<std::array<BlockType, SIZE * SIZE * SIZE>>();
    (*m_blocks)[x + y * SIZE + z * SIZE * SIZE] = type;
}

BlockType Chunk::getBlockSafe(int x, int y, int z) const {
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE || z < 0 || z >= SIZE) {
        if (m_blockQuery) {
            return m_blockQuery(m_chunkX * SIZE + x, m_chunkY * SIZE + y, m_chunkZ * SIZE + z);
        }
        return BlockType::Air;
    }
    return getBlock(x, y, z);
}

void Chunk::clearBlocks() {
    m_blocks.reset();
}

// ── Terrain generation ────────────────────────────────────────

void Chunk::generate(const TerrainGenerator& gen, int chunkX, int chunkY, int chunkZ, int lodLevel) {
    m_chunkX = chunkX;
    m_chunkY = chunkY;
    m_chunkZ = chunkZ;

    if (lodLevel != m_lodLevel || (1 << lodLevel) != m_step) {
        setLodLevel(lodLevel);
    }

    m_blocks = std::make_shared<std::array<BlockType, SIZE * SIZE * SIZE>>();
    m_nonAirCount = 0;

    for (int x = 0; x < SIZE; x += m_step) {
        for (int z = 0; z < SIZE; z += m_step) {
            float worldX = static_cast<float>(chunkX * SIZE + x);
            float worldZ = static_cast<float>(chunkZ * SIZE + z);

            for (int y = 0; y < SIZE; y += m_step) {
                int worldY = chunkY * SIZE + y;
                BlockType type = gen.getBlock(worldX, static_cast<float>(worldY), worldZ);
                if (type == BlockType::Air) continue;
                setBlock(x, y, z, type);
                m_nonAirCount++;
            }
        }
    }
}

// ── Mesh building ─────────────────────────────────────────────

float Chunk::calcVertexAO(int x, int y, int z, int face, int vertex) {
    int nx = FACE_NORMALS[face][0];
    int ny = FACE_NORMALS[face][1];
    int nz = FACE_NORMALS[face][2];

    int u = FACE_VERTEX_UV[face][vertex][0];
    int v = FACE_VERTEX_UV[face][vertex][1];

    int step = m_step;

    int t1x = FACE_TANGENT1[face][0] * step;
    int t1y = FACE_TANGENT1[face][1] * step;
    int t1z = FACE_TANGENT1[face][2] * step;

    int t2x = FACE_TANGENT2[face][0] * step;
    int t2y = FACE_TANGENT2[face][1] * step;
    int t2z = FACE_TANGENT2[face][2] * step;

    int sx = x + nx * step + u * t1x;
    int sy = y + ny * step + u * t1y;
    int sz = z + nz * step + u * t1z;
    bool side1 = isBlockSolid(getBlockSafe(sx, sy, sz));

    int sx2 = x + nx * step + v * t2x;
    int sy2 = y + ny * step + v * t2y;
    int sz2 = z + nz * step + v * t2z;
    bool side2 = isBlockSolid(getBlockSafe(sx2, sy2, sz2));

    int cx = x + nx * step + u * t1x + v * t2x;
    int cy = y + ny * step + u * t1y + v * t2y;
    int cz = z + nz * step + u * t1z + v * t2z;
    bool corner = isBlockSolid(getBlockSafe(cx, cy, cz));

    if (side1 && side2) return 0.0f;
    return (3.0f - static_cast<float>(side1 + side2 + corner)) / 3.0f;
}

void Chunk::addFace(int x, int y, int z, int face, const glm::vec3& color, const float ao[4]) {
    uint32_t base = static_cast<uint32_t>(m_vertices.size());
    int step = m_step;

    for (int i = 0; i < 4; ++i) {
        VertexCPU v;
        v.position = glm::vec3(
            (x + FACE_VERTICES[face][i][0] * step) * BLOCK_SIZE,
            (y + FACE_VERTICES[face][i][1] * step) * BLOCK_SIZE,
            (z + FACE_VERTICES[face][i][2] * step) * BLOCK_SIZE);
        v.normal = glm::vec3(
            FACE_NORMALS[face][0],
            FACE_NORMALS[face][1],
            FACE_NORMALS[face][2]);
        v.color = color;
        v.ao = ao[i];
        m_vertices.push_back(v);
    }

    for (int i = 0; i < 6; ++i) {
        m_indices.push_back(base + FACE_INDICES[i]);
    }
}

void Chunk::buildMesh(std::function<BlockType(int, int, int)> blockQuery) {
    m_blockQuery = std::move(blockQuery);
    m_vertices.clear();
    m_indices.clear();

    static constexpr size_t MAX_VERTICES = 65000;
    m_vertices.reserve(MAX_VERTICES + 4);
    m_indices.reserve(MAX_VERTICES * 6);
    bool hitLimit = false;

    int step = m_step;

    for (int x = 0; x < SIZE && !hitLimit; x += step) {
        for (int y = 0; y < SIZE && !hitLimit; y += step) {
            for (int z = 0; z < SIZE && !hitLimit; z += step) {
                BlockType block = getBlock(x, y, z);
                if (block == BlockType::Air) continue;

                glm::vec3 color = getBlockColor(block);

                for (int face = 0; face < 6; ++face) {
                    if (m_vertices.size() + 4 > MAX_VERTICES) {
                        hitLimit = true;
                        break;
                    }

                    int nx = x + step * FACE_NORMALS[face][0];
                    int ny = y + step * FACE_NORMALS[face][1];
                    int nz = z + step * FACE_NORMALS[face][2];

                    if (!isBlockSolid(getBlockSafe(nx, ny, nz))) {
                        float ao[4];
                        for (int v = 0; v < 4; ++v) {
                            ao[v] = calcVertexAO(x, y, z, face, v);
                        }
                        addFace(x, y, z, face, color, ao);
                    }
                }
            }
        }
    }
}

// ── GPU upload ────────────────────────────────────────────────

void Chunk::uploadToGPU() {
    if (m_vertices.empty()) return;
    uploadFromData(std::move(m_vertices), std::move(m_indices));
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_indices.clear();
    m_indices.shrink_to_fit();
}

void Chunk::uploadFromData(std::vector<VertexCPU> verts, std::vector<uint32_t> indices) {
    if (verts.empty()) return;

    int vertCount = static_cast<int>(verts.size());

    m_mesh.vertexCount = vertCount;
    m_mesh.triangleCount = static_cast<int>(indices.size() / 3);

    m_mesh.vertices = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    m_mesh.normals = (float*)RL_MALLOC(vertCount * 3 * sizeof(float));
    m_mesh.texcoords = (float*)RL_MALLOC(vertCount * 2 * sizeof(float));
    m_mesh.colors = (unsigned char*)RL_MALLOC(vertCount * 4 * sizeof(unsigned char));
    m_mesh.indices = (unsigned short*)RL_MALLOC(indices.size() * sizeof(unsigned short));

    for (int i = 0; i < vertCount; ++i) {
        m_mesh.vertices[i * 3 + 0] = verts[i].position.x;
        m_mesh.vertices[i * 3 + 1] = verts[i].position.y;
        m_mesh.vertices[i * 3 + 2] = verts[i].position.z;

        m_mesh.normals[i * 3 + 0] = verts[i].normal.x;
        m_mesh.normals[i * 3 + 1] = verts[i].normal.y;
        m_mesh.normals[i * 3 + 2] = verts[i].normal.z;

        m_mesh.texcoords[i * 2 + 0] = 0.0f;
        m_mesh.texcoords[i * 2 + 1] = 0.0f;

        float ao = verts[i].ao;
        m_mesh.colors[i * 4 + 0] = (unsigned char)(verts[i].color.x * ao * 255.0f);
        m_mesh.colors[i * 4 + 1] = (unsigned char)(verts[i].color.y * ao * 255.0f);
        m_mesh.colors[i * 4 + 2] = (unsigned char)(verts[i].color.z * ao * 255.0f);
        m_mesh.colors[i * 4 + 3] = 255;
    }

    for (size_t i = 0; i < indices.size(); ++i) {
        m_mesh.indices[i] = static_cast<unsigned short>(indices[i]);
    }

    UploadMesh(&m_mesh, false);
    m_hasMesh = true;
}

void Chunk::unloadGPU() {
    if (m_hasMesh) {
        UnloadMesh(m_mesh);
        m_mesh = {};
        m_hasMesh = false;
    }
}
