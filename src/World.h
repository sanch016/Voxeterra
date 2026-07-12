#pragma once

#include "Chunk.h"
#include "TerrainGenerator.h"
#include "ThreadPool.h"

#include <glm/glm.hpp>

#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <vector>

class World {
public:
    explicit World(int renderDistance = 4);
    ~World() = default;

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void update(glm::vec3 cameraPos);
    BlockType getBlock(int worldX, int worldY, int worldZ) const;
    void setBlock(int worldX, int worldY, int worldZ, BlockType type);
    const std::vector<Chunk*>& getChunksToRender() const { return m_renderList; }
    size_t getPendingCount() const { return m_pendingChunks.size(); }
    size_t getWithoutMeshCount() const { return m_chunksWithoutMesh.size(); }
    size_t getMeshedCount() const { return m_chunks.size(); }
    size_t getQueuedCount() const { return m_queuedChunks.size(); }
    TerrainParams& terrainParams() { return m_terrainParams; }
    int getRenderDistance() const { return m_renderDistance; }
    int getVerticalRange() const { return m_verticalRange; }
    int getChunksPerFrame() const { return m_chunksPerFrame; }
    void setRenderDistance(int rd);
    void setVerticalRange(int vr);
    void setChunksPerFrame(int cpf);
    void applyTerrainParams();

    int getLodDistance(int lod) const { return m_lodDistances[lod]; }
    void setLodDistance(int lod, int dist);
    void getLodCounts(int counts[Chunk::NUM_LODS]) const;

private:
    struct ChunkKey {
        int x, y, z;
        bool operator<(const ChunkKey& o) const {
            if (x != o.x) return x < o.x;
            if (y != o.y) return y < o.y;
            return z < o.z;
        }
    };

    struct PendingEntry {
        ChunkKey key;
        int lodLevel = 0;
    };

    struct MeshBuildResult {
        ChunkKey key;
        std::unique_ptr<Chunk> chunk;
    };

    void fillPendingQueue(int cx, int cy, int cz);
    void processPendingChunks();
    void buildReadyMeshes(int camCX, int camCY, int camCZ);
    void uploadReadyMeshes();
    void rebuildRenderList();
    void evictDistantChunks(int camCX, int camCY, int camCZ);
    void cleanupQueuedChunks();

    int m_chunksPerFrame = 32;
    int m_verticalRange = 8;
    std::vector<PendingEntry> m_pendingChunks;

    std::map<ChunkKey, std::unique_ptr<Chunk>> m_chunks;
    std::map<ChunkKey, std::unique_ptr<Chunk>> m_chunksWithoutMesh;
    mutable std::mutex m_chunksMutex;

    std::set<ChunkKey> m_queuedChunks;
    std::mutex m_queuedMutex;

    std::vector<Chunk*> m_renderList;

    std::vector<MeshBuildResult> m_uploadQueue;
    std::mutex m_uploadMutex;

    ThreadPool m_threadPool;
    int m_renderDistance;
    int m_lodDistances[Chunk::NUM_LODS] = {6, 14, 24, 40};

    TerrainParams m_terrainParams;
    TerrainGenerator m_terrainGen;

    int m_lastCenterX = 0x7FFFFFFF;
    int m_lastCenterY = 0x7FFFFFFF;
    int m_lastCenterZ = 0x7FFFFFFF;

    int m_framesSinceStart = 0;
};
