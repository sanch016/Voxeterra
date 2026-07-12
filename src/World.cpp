#include "World.h"

#include <algorithm>
#include <cmath>

World::World(int renderDistance)
    : m_threadPool(std::thread::hardware_concurrency() > 0
                       ? std::thread::hardware_concurrency() : 4)
    , m_renderDistance(renderDistance)
    , m_terrainGen(m_terrainParams)
{
}

BlockType World::getBlock(int worldX, int worldY, int worldZ) const {
    int bx = static_cast<int>(std::floor(static_cast<float>(worldX) / BLOCK_SIZE));
    int by = static_cast<int>(std::floor(static_cast<float>(worldY) / BLOCK_SIZE));
    int bz = static_cast<int>(std::floor(static_cast<float>(worldZ) / BLOCK_SIZE));

    int cx = static_cast<int>(std::floor(static_cast<float>(bx) / Chunk::SIZE));
    int cy = static_cast<int>(std::floor(static_cast<float>(by) / Chunk::SIZE));
    int cz = static_cast<int>(std::floor(static_cast<float>(bz) / Chunk::SIZE));

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find({cx, cy, cz});
    if (it == m_chunks.end()) return BlockType::Air;

    int lx = bx - cx * Chunk::SIZE;
    int ly = by - cy * Chunk::SIZE;
    int lz = bz - cz * Chunk::SIZE;

    return it->second->getBlock(lx, ly, lz);
}

void World::update(glm::vec3 cameraPos) {
    buildReadyMeshes();

    int camCX = static_cast<int>(std::floor(cameraPos.x / (Chunk::SIZE * BLOCK_SIZE)));
    int camCY = static_cast<int>(std::floor(cameraPos.y / (Chunk::SIZE * BLOCK_SIZE)));
    int camCZ = static_cast<int>(std::floor(cameraPos.z / (Chunk::SIZE * BLOCK_SIZE)));

    if (camCX != m_lastCenterX || camCY != m_lastCenterY || camCZ != m_lastCenterZ) {
        m_lastCenterX = camCX;
        m_lastCenterY = camCY;
        m_lastCenterZ = camCZ;
        loadChunksAround(camCX, camCY, camCZ);
    }

    processPendingChunks();
}

void World::loadChunksAround(int cx, int cy, int cz) {
    m_pendingChunks.clear();

    for (int dx = -m_renderDistance; dx <= m_renderDistance; ++dx) {
        for (int dz = -m_renderDistance; dz <= m_renderDistance; ++dz) {
            if (dx * dx + dz * dz > m_renderDistance * m_renderDistance) continue;

            for (int dy = -m_verticalRange; dy <= m_verticalRange; ++dy) {
                ChunkKey key{cx + dx, cy + dy, cz + dz};

                {
                    std::lock_guard lock(m_queuedMutex);
                    if (m_queuedChunks.count(key)) continue;
                }

                {
                    std::lock_guard lock(m_chunksMutex);
                    if (m_chunks.count(key) || m_chunksWithoutMesh.count(key)) continue;
                }

                m_pendingChunks.push_back(key);
            }
        }
    }

    std::sort(m_pendingChunks.begin(), m_pendingChunks.end(),
        [cx, cy, cz](const ChunkKey& a, const ChunkKey& b) {
            int da = (a.x - cx) * (a.x - cx) + (a.y - cy) * (a.y - cy) + (a.z - cz) * (a.z - cz);
            int db = (b.x - cx) * (b.x - cx) + (b.y - cy) * (b.y - cy) + (b.z - cz) * (b.z - cz);
            return da < db;
        });
}

void World::processPendingChunks() {
    static int totalSubmitted = 0;
    int submitted = 0;
    auto it = m_pendingChunks.begin();
    while (it != m_pendingChunks.end() && submitted < m_chunksPerFrame) {
        ChunkKey key = *it;

        {
            std::lock_guard lock(m_queuedMutex);
            if (m_queuedChunks.count(key)) {
                it = m_pendingChunks.erase(it);
                continue;
            }
            m_queuedChunks.insert(key);
        }

        {
            std::lock_guard lock(m_chunksMutex);
            if (m_chunks.count(key) || m_chunksWithoutMesh.count(key)) {
                it = m_pendingChunks.erase(it);
                continue;
            }
        }

        m_threadPool.enqueue([this, key]() {
            auto chunk = std::make_unique<Chunk>();
            chunk->generate(m_terrainGen, key.x, key.y, key.z);

            std::lock_guard lock(m_chunksMutex);
            m_chunksWithoutMesh[key] = std::move(chunk);
        });

        it = m_pendingChunks.erase(it);
        submitted++;
    }
}

void World::buildReadyMeshes() {
    std::vector<ChunkKey> readyKeys;

    {
        std::lock_guard lock(m_chunksMutex);
        for (auto& [key, chunk] : m_chunksWithoutMesh) {
            bool ready = true;
            for (auto [dx, dz] : std::vector<std::pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1}}) {
                ChunkKey nk{key.x + dx, key.y, key.z + dz};
                if (m_chunksWithoutMesh.count(nk) == 0 && m_chunks.count(nk) == 0) {
                    ready = false;
                    break;
                }
            }
            if (ready) readyKeys.push_back(key);
        }
    }

    bool anyUploaded = false;
    for (auto& key : readyKeys) {
        std::unique_ptr<Chunk> chunk;
        {
            std::lock_guard lock(m_chunksMutex);
            auto it = m_chunksWithoutMesh.find(key);
            if (it == m_chunksWithoutMesh.end()) continue;
            chunk = std::move(it->second);
            m_chunksWithoutMesh.erase(it);
        }

        chunk->buildMesh([this](int wx, int wy, int wz) -> BlockType {
            int cx = static_cast<int>(std::floor(static_cast<float>(wx) / Chunk::SIZE));
            int cy = static_cast<int>(std::floor(static_cast<float>(wy) / Chunk::SIZE));
            int cz = static_cast<int>(std::floor(static_cast<float>(wz) / Chunk::SIZE));

            std::lock_guard lock(m_chunksMutex);
            ChunkKey nk{cx, cy, cz};

            auto it1 = m_chunks.find(nk);
            if (it1 != m_chunks.end()) {
                int lx = wx - cx * Chunk::SIZE;
                int ly = wy - cy * Chunk::SIZE;
                int lz = wz - cz * Chunk::SIZE;
                return it1->second->getBlock(lx, ly, lz);
            }

            auto it2 = m_chunksWithoutMesh.find(nk);
            if (it2 != m_chunksWithoutMesh.end()) {
                int lx = wx - cx * Chunk::SIZE;
                int ly = wy - cy * Chunk::SIZE;
                int lz = wz - cz * Chunk::SIZE;
                return it2->second->getBlock(lx, ly, lz);
            }

            return BlockType::Air;
        });

        chunk->uploadToGPU();

        {
            std::lock_guard lock(m_chunksMutex);
            m_chunks[key] = std::move(chunk);

            auto tryFreeBlocks = [this](const ChunkKey& k) {
                auto it = m_chunks.find(k);
                if (it == m_chunks.end() || !it->second->hasBlocks()) return;
                for (auto [dx, dz] : std::vector<std::pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1}}) {
                    ChunkKey nk{k.x + dx, k.y, k.z + dz};
                    if (m_chunks.count(nk) == 0) return;
                }
                it->second->clearBlocks();
            };

            tryFreeBlocks(key);
            for (auto [dx, dz] : std::vector<std::pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1}}) {
                tryFreeBlocks({key.x + dx, key.y, key.z + dz});
            }
        }
        anyUploaded = true;
    }

    if (anyUploaded) {
        rebuildRenderList();
    }
}

void World::rebuildRenderList() {
    m_renderList.clear();
    std::lock_guard lock(m_chunksMutex);
    m_renderList.reserve(m_chunks.size());
    for (auto& [key, chunk] : m_chunks) {
        if (chunk->hasMesh()) {
            m_renderList.push_back(chunk.get());
        }
    }
}

void World::setRenderDistance(int rd) {
    if (rd < 1) rd = 1;
    if (rd == m_renderDistance) return;
    m_renderDistance = rd;
    m_lastCenterX = 0x7FFFFFFF;
}

void World::setVerticalRange(int vr) {
    if (vr < 1) vr = 1;
    if (vr == m_verticalRange) return;
    m_verticalRange = vr;
    m_lastCenterX = 0x7FFFFFFF;
}

void World::setChunksPerFrame(int cpf) {
    if (cpf < 1) cpf = 1;
    m_chunksPerFrame = cpf;
}

void World::applyTerrainParams() {
    m_terrainGen.setParams(m_terrainParams);

    std::lock_guard lock(m_chunksMutex);
    m_chunks.clear();
    m_chunksWithoutMesh.clear();
    m_renderList.clear();

    std::lock_guard qlock(m_queuedMutex);
    m_queuedChunks.clear();

    m_lastCenterX = 0x7FFFFFFF;
}
