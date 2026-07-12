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

BlockType World::getBlock(int blockX, int blockY, int blockZ) const {
    int cx = static_cast<int>(std::floor(static_cast<float>(blockX) / Chunk::SIZE));
    int cy = static_cast<int>(std::floor(static_cast<float>(blockY) / Chunk::SIZE));
    int cz = static_cast<int>(std::floor(static_cast<float>(blockZ) / Chunk::SIZE));

    std::lock_guard lock(m_chunksMutex);
    auto it = m_chunks.find({cx, cy, cz});
    if (it != m_chunks.end()) {
        int lx = blockX - cx * Chunk::SIZE;
        int ly = blockY - cy * Chunk::SIZE;
        int lz = blockZ - cz * Chunk::SIZE;
        return it->second->getBlock(lx, ly, lz);
    }
    // Check chunks being remeshed (in m_chunksWithoutMesh but not yet in m_chunks)
    auto it2 = m_chunksWithoutMesh.find({cx, cy, cz});
    if (it2 != m_chunksWithoutMesh.end()) {
        int lx = blockX - cx * Chunk::SIZE;
        int ly = blockY - cy * Chunk::SIZE;
        int lz = blockZ - cz * Chunk::SIZE;
        return it2->second->getBlock(lx, ly, lz);
    }
    return BlockType::Air;
}

void World::setBlock(int blockX, int blockY, int blockZ, BlockType type) {
    using BlockArray = std::array<BlockType, Chunk::SIZE * Chunk::SIZE * Chunk::SIZE>;

    int cx = static_cast<int>(std::floor(static_cast<float>(blockX) / Chunk::SIZE));
    int cy = static_cast<int>(std::floor(static_cast<float>(blockY) / Chunk::SIZE));
    int cz = static_cast<int>(std::floor(static_cast<float>(blockZ) / Chunk::SIZE));

    int lx = blockX - cx * Chunk::SIZE;
    int ly = blockY - cy * Chunk::SIZE;
    int lz = blockZ - cz * Chunk::SIZE;

    // Take ALL snapshots under one lock, then submit tasks outside
    std::map<ChunkKey, std::shared_ptr<BlockArray>> snapshots;
    std::vector<ChunkKey> keysToRemesh;

    {
        std::lock_guard lock(m_chunksMutex);

        auto it = m_chunks.find({cx, cy, cz});
        if (it != m_chunks.end()) {
            it->second->setBlock(lx, ly, lz, type);
            snapshots[{cx, cy, cz}] = std::make_shared<BlockArray>(*it->second->shareBlocks());
            keysToRemesh.push_back({cx, cy, cz});
        } else {
            auto it2 = m_chunksWithoutMesh.find({cx, cy, cz});
            if (it2 != m_chunksWithoutMesh.end()) {
                it2->second->setBlock(lx, ly, lz, type);
            }
            return;
        }

        auto addSnapshot = [&](int dx, int dy, int dz) {
            ChunkKey nk{cx + dx, cy + dy, cz + dz};
            auto nit = m_chunks.find(nk);
            if (nit == m_chunks.end()) return;
            snapshots[nk] = std::make_shared<BlockArray>(*nit->second->shareBlocks());
            keysToRemesh.push_back(nk);
        };

        if (lx == 0)          addSnapshot(-1, 0,  0);
        if (lx == Chunk::SIZE - 1) addSnapshot( 1, 0,  0);
        if (ly == 0)          addSnapshot( 0, -1, 0);
        if (ly == Chunk::SIZE - 1) addSnapshot( 0,  1, 0);
        if (lz == 0)          addSnapshot( 0,  0, -1);
        if (lz == Chunk::SIZE - 1) addSnapshot( 0,  0,  1);
    }

    for (auto& key : keysToRemesh) {
        auto it = m_chunks.find(key);
        if (it == m_chunks.end()) continue;
        auto blocksCopy = std::make_shared<BlockArray>(*it->second->shareBlocks());
        int step = it->second->getStep();
        int cxx = key.x, cyy = key.y, czz = key.z;
        TerrainGenerator genCopy = m_terrainGen;
        m_threadPool.enqueue([this, key, blocksCopy, step, cxx, cyy, czz, genCopy, snapshots]() {
            auto temp = std::make_unique<Chunk>();
            temp->setPosition(cxx, cyy, czz);
            temp->setLodLevel(0);
            temp->assignBlocks(blocksCopy);
            temp->buildMesh([genCopy, snapshots](int wx, int wy, int wz) -> BlockType {
                int bcx = static_cast<int>(std::floor(static_cast<float>(wx) / Chunk::SIZE));
                int bcy = static_cast<int>(std::floor(static_cast<float>(wy) / Chunk::SIZE));
                int bcz = static_cast<int>(std::floor(static_cast<float>(wz) / Chunk::SIZE));
                ChunkKey nk{bcx, bcy, bcz};
                auto sit = snapshots.find(nk);
                if (sit != snapshots.end()) {
                    int lx = wx - bcx * Chunk::SIZE;
                    int ly = wy - bcy * Chunk::SIZE;
                    int lz = wz - bcz * Chunk::SIZE;
                    return (*sit->second)[lx + ly * Chunk::SIZE + lz * Chunk::SIZE * Chunk::SIZE];
                }
                return genCopy.getBlock(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz));
            });
            std::lock_guard lock(m_uploadMutex);
            m_uploadQueue.push_back({key, std::move(temp)});
        });
    }
}

void World::update(glm::vec3 cameraPos) {
    uploadReadyMeshes();

    int camCX = static_cast<int>(std::floor(cameraPos.x / (Chunk::SIZE * BLOCK_SIZE)));
    int camCY = static_cast<int>(std::floor(cameraPos.y / (Chunk::SIZE * BLOCK_SIZE)));
    int camCZ = static_cast<int>(std::floor(cameraPos.z / (Chunk::SIZE * BLOCK_SIZE)));

    m_lastCenterX = camCX;
    m_lastCenterY = camCY;
    m_lastCenterZ = camCZ;

    buildReadyMeshes(camCX, camCY, camCZ);
    fillPendingQueue(camCX, camCY, camCZ);
    processPendingChunks();

    m_framesSinceStart++;

    if (m_framesSinceStart % 30 == 0 && m_framesSinceStart > 0) {
        evictDistantChunks(camCX, camCY, camCZ);
        cleanupQueuedChunks();
    }
}

void World::fillPendingQueue(int cx, int cy, int cz) {
    static constexpr size_t MAX_PENDING = 64;

    int rdSq = m_renderDistance * m_renderDistance;

    m_pendingChunks.erase(
        std::remove_if(m_pendingChunks.begin(), m_pendingChunks.end(),
            [&](const PendingEntry& e) {
                int dx = e.key.x - cx;
                int dz = e.key.z - cz;
                return (dx * dx + dz * dz) > rdSq
                    || std::abs(e.key.y - cy) > m_verticalRange;
            }),
        m_pendingChunks.end());

    if (m_pendingChunks.size() >= MAX_PENDING) return;

    // Collect existing chunks and their LOD levels
    std::map<ChunkKey, int> existingLod;
    {
        std::lock_guard lock(m_queuedMutex);
        for (auto& key : m_queuedChunks)
            existingLod[key] = 0; // treat queued as best LOD
    }
    {
        std::lock_guard lock(m_chunksMutex);
        for (auto& [key, chunk] : m_chunks)
            existingLod[key] = chunk->getLodLevel();
        for (auto& [key, chunk] : m_chunksWithoutMesh)
            existingLod[key] = chunk->getLodLevel();
    }
    {
        std::lock_guard lock(m_uploadMutex);
        for (auto& entry : m_uploadQueue)
            existingLod[entry.key] = 0; // treat as best LOD — in transit
    }
    for (auto& entry : m_pendingChunks) {
        auto it = existingLod.find(entry.key);
        if (it == existingLod.end() || entry.lodLevel < it->second)
            existingLod[entry.key] = entry.lodLevel;
    }

    std::vector<PendingEntry> candidates;
    for (int dx = -m_renderDistance; dx <= m_renderDistance; ++dx) {
        for (int dz = -m_renderDistance; dz <= m_renderDistance; ++dz) {
            if (dx * dx + dz * dz > rdSq) continue;
            for (int dy = -m_verticalRange; dy <= m_verticalRange; ++dy) {
                ChunkKey key{cx + dx, cy + dy, cz + dz};

                int dist2 = dx * dx + dy * dy + dz * dz;
                int lod = Chunk::NUM_LODS - 1;
                for (int i = 0; i < Chunk::NUM_LODS; ++i) {
                    if (dist2 <= m_lodDistances[i] * m_lodDistances[i]) {
                        lod = i;
                        break;
                    }
                }

                auto existing = existingLod.find(key);
                if (existing != existingLod.end() && existing->second <= lod)
                    continue;

                candidates.push_back({key, lod});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [cx, cy, cz](const PendingEntry& a, const PendingEntry& b) {
            int da = (a.key.x - cx) * (a.key.x - cx)
                   + (a.key.y - cy) * (a.key.y - cy)
                   + (a.key.z - cz) * (a.key.z - cz);
            int db = (b.key.x - cx) * (b.key.x - cx)
                   + (b.key.y - cy) * (b.key.y - cy)
                   + (b.key.z - cz) * (b.key.z - cz);
            return da < db;
        });

    size_t space = MAX_PENDING - m_pendingChunks.size();
    size_t toAdd = std::min(candidates.size(), space);
    for (size_t i = 0; i < toAdd; ++i) {
        m_pendingChunks.push_back(candidates[i]);
    }
}

void World::processPendingChunks() {
    int submitted = 0;
    TerrainGenerator genCopy = m_terrainGen;

    auto it = m_pendingChunks.begin();
    while (it != m_pendingChunks.end() && submitted < m_chunksPerFrame) {
        ChunkKey key = it->key;
        int lod = it->lodLevel;

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
            auto ex1 = m_chunks.find(key);
            if (ex1 != m_chunks.end()) {
                if (ex1->second->getLodLevel() <= lod) {
                    it = m_pendingChunks.erase(it);
                    continue;
                }
                ex1->second->unloadGPU();
                m_chunks.erase(ex1);
            } else {
                auto ex2 = m_chunksWithoutMesh.find(key);
                if (ex2 != m_chunksWithoutMesh.end()) {
                    if (ex2->second->getLodLevel() <= lod) {
                        it = m_pendingChunks.erase(it);
                        continue;
                    }
                    m_chunksWithoutMesh.erase(ex2);
                }
            }
        }

        m_threadPool.enqueue([this, key, genCopy, lod]() {
            auto chunk = std::make_unique<Chunk>();

            // ── Quick surface-height check: skip if entirely above terrain ──
            if (lod == 0) {
                int chunkMinY = key.y * Chunk::SIZE;
                int maxSurface = -999;
                for (int sx : {0, Chunk::SIZE / 2, Chunk::SIZE - 1}) {
                    for (int sz : {0, Chunk::SIZE / 2, Chunk::SIZE - 1}) {
                        auto col = genCopy.sampleColumn(
                            static_cast<float>(key.x * Chunk::SIZE + sx),
                            static_cast<float>(key.z * Chunk::SIZE + sz));
                        if (col.surfaceHeight > maxSurface)
                            maxSurface = col.surfaceHeight;
                    }
                }
                if (chunkMinY > maxSurface + 8) {
                    return; // entirely above max surface — all Air, skip
                }
            }

            chunk->generate(genCopy, key.x, key.y, key.z, lod);

            std::lock_guard lock(m_chunksMutex);
            if (chunk->isEmpty()) {
                return; // all Air, nothing to store
            }
            if (chunk->isFullySolid()) {
                // all solid blocks — no mesh possible, store directly
                m_chunks[key] = std::move(chunk);
                return;
            }
            m_chunksWithoutMesh[key] = std::move(chunk);
        });

        it = m_pendingChunks.erase(it);
        submitted++;
    }
}

void World::buildReadyMeshes(int camCX, int camCY, int camCZ) {
    using BlockArray = std::array<BlockType, Chunk::SIZE * Chunk::SIZE * Chunk::SIZE>;
    static constexpr std::tuple<int,int,int> NEIGHBORS[6] = {
        {1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}
    };

    struct ReadyEntry {
        ChunkKey key;
        std::unique_ptr<Chunk> chunk;
        std::map<ChunkKey, std::shared_ptr<BlockArray>> snaps;
        std::set<ChunkKey> skipLodNeighbors; // neighbors at a different LOD — don't cull against them
    };

    std::vector<ReadyEntry> ready;
    std::vector<ReadyEntry> readyLod;
    int totalBudget = m_chunksPerFrame;

    {
        std::lock_guard lock(m_chunksMutex);

        // ── Pull LOD > 0 chunks out for direct meshing ──
        for (auto it = m_chunksWithoutMesh.begin(); it != m_chunksWithoutMesh.end(); ) {
            if (it->second->getLodLevel() > 0) {
                readyLod.push_back({it->first, std::move(it->second), {}});
                it = m_chunksWithoutMesh.erase(it);
            } else {
                ++it;
            }
        }

        // ── Pass 1 & 2 for LOD 0 chunks (with neighbor snapshots) ──
        for (int pass = 1; pass <= 2; ++pass) {
            int remaining = totalBudget - static_cast<int>(ready.size());
            if (remaining <= 0) break;

            int thisBudget = (pass == 1)
                ? std::max(1, remaining - 4)
                : remaining;

            std::vector<ChunkKey> readyKeys;
            for (auto& [key, _] : m_chunksWithoutMesh) {
                if (static_cast<int>(readyKeys.size()) >= thisBudget) break;

                if (pass == 1) {
                    bool allOk = true;
                    for (auto [dx, dy, dz] : NEIGHBORS) {
                        ChunkKey nk{key.x + dx, key.y + dy, key.z + dz};
                        if (!m_chunks.count(nk) && !m_chunksWithoutMesh.count(nk)) {
                            allOk = false;
                            break;
                        }
                    }
                    if (allOk) readyKeys.push_back(key);
                } else {
                    readyKeys.push_back(key);
                }
            }

            if (readyKeys.empty()) continue;

            std::map<ChunkKey, std::map<ChunkKey, std::shared_ptr<BlockArray>>> allSnaps;
            std::map<ChunkKey, std::set<ChunkKey>> mismatchedLod;
            for (auto& key : readyKeys) {
                std::map<ChunkKey, std::shared_ptr<BlockArray>> snaps;
                std::set<ChunkKey> mismatched;
                for (auto [dx, dy, dz] : NEIGHBORS) {
                    ChunkKey nk{key.x + dx, key.y + dy, key.z + dz};
                    auto it1 = m_chunks.find(nk);
                    if (it1 != m_chunks.end() && it1->second->hasBlocks()) {
                        if (it1->second->getLodLevel() > 0) {
                            mismatched.insert(nk);
                            continue;
                        }
                        snaps[nk] = it1->second->shareBlocks();
                        continue;
                    }
                    auto it2 = m_chunksWithoutMesh.find(nk);
                    if (it2 != m_chunksWithoutMesh.end() && it2->second->hasBlocks()) {
                        if (it2->second->getLodLevel() > 0) {
                            mismatched.insert(nk);
                            continue;
                        }
                        snaps[nk] = it2->second->shareBlocks();
                    }
                }
                allSnaps[key] = std::move(snaps);
                mismatchedLod[key] = std::move(mismatched);
            }

            for (auto& key : readyKeys) {
                auto kit = m_chunksWithoutMesh.find(key);
                auto chunk = std::move(kit->second);
                m_chunksWithoutMesh.erase(kit);
                ready.push_back({key, std::move(chunk), std::move(allSnaps[key]), std::move(mismatchedLod[key])});
            }
        }
    }

    // Protect these chunks from being re-queued by fillPendingQueue
    {
        std::lock_guard qlock(m_queuedMutex);
        for (auto& entry : ready)
            m_queuedChunks.insert(entry.key);
        for (auto& entry : readyLod)
            m_queuedChunks.insert(entry.key);
    }

    // ── Submit LOD 0 mesh jobs ──
    for (auto& entry : ready) {
        ChunkKey key = entry.key;
        std::set<ChunkKey> skipLod = std::move(entry.skipLodNeighbors);
        m_threadPool.enqueue([this, key, chunk = std::move(entry.chunk), snaps = std::move(entry.snaps), skipLod = std::move(skipLod)]() mutable {
            int cx = key.x, cy = key.y, cz = key.z;
            chunk->buildMesh([&snaps, &skipLod, this, cx, cy, cz](int wx, int wy, int wz) -> BlockType {
                int bcx = static_cast<int>(std::floor(static_cast<float>(wx) / Chunk::SIZE));
                int bcy = static_cast<int>(std::floor(static_cast<float>(wy) / Chunk::SIZE));
                int bcz = static_cast<int>(std::floor(static_cast<float>(wz) / Chunk::SIZE));

                ChunkKey nk{bcx, bcy, bcz};
                if (skipLod.count(nk)) {
                    return BlockType::Air;
                }
                auto it = snaps.find(nk);
                if (it != snaps.end()) {
                    int lx = wx - bcx * Chunk::SIZE;
                    int ly = wy - bcy * Chunk::SIZE;
                    int lz = wz - bcz * Chunk::SIZE;
                    return (*it->second)[lx + ly * Chunk::SIZE + lz * Chunk::SIZE * Chunk::SIZE];
                }
                return m_terrainGen.getBlock(
                    static_cast<float>(wx),
                    static_cast<float>(wy),
                    static_cast<float>(wz));
            });

            std::lock_guard lock(m_uploadMutex);
            m_uploadQueue.push_back({key, std::move(chunk)});
        });
    }

    // ── Submit LOD > 0 mesh jobs (no neighbor snapshots, terrain fallback) ──
    if (!readyLod.empty()) {
        TerrainGenerator genCopy = m_terrainGen;
        for (auto& entry : readyLod) {
            ChunkKey key = entry.key;
            m_threadPool.enqueue([this, key, chunk = std::move(entry.chunk), genCopy]() mutable {
                chunk->buildMesh([genCopy](int wx, int wy, int wz) -> BlockType {
                    return genCopy.getBlock(
                        static_cast<float>(wx),
                        static_cast<float>(wy),
                        static_cast<float>(wz));
                });

                std::lock_guard lock(m_uploadMutex);
                m_uploadQueue.push_back({key, std::move(chunk)});
            });
        }
    }
}

void World::uploadReadyMeshes() {
    std::vector<MeshBuildResult> results;
    {
        std::lock_guard lock(m_uploadMutex);
        if (m_uploadQueue.empty()) return;
        results = std::move(m_uploadQueue);
    }

    bool anyUploaded = false;
    for (auto& res : results) {
        std::lock_guard lock(m_chunksMutex);
        auto existing = m_chunks.find(res.key);
        if (existing != m_chunks.end()) {
            // In-place mesh update: chunk stayed in m_chunks (from setBlock)
            existing->second->unloadGPU();
            existing->second->uploadFromData(res.chunk->takeVertices(), res.chunk->takeIndices());
        } else {
            // Normal path: chunk comes from m_chunksWithoutMesh
            res.chunk->uploadToGPU();
            m_chunks[res.key] = std::move(res.chunk);
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

void World::evictDistantChunks(int camCX, int camCY, int camCZ) {
    int evictionDist = m_renderDistance + 8;
    std::vector<ChunkKey> toEvict;
    bool anyEvicted = false;

    {
        std::lock_guard lock(m_chunksMutex);

        for (auto& [key, chunk] : m_chunks) {
            int dx = key.x - camCX;
            int dy = key.y - camCY;
            int dz = key.z - camCZ;
            if (dx * dx + dy * dy + dz * dz > evictionDist * evictionDist) {
                toEvict.push_back(key);
            }
        }
        for (auto& key : toEvict) {
            auto it = m_chunks.find(key);
            if (it != m_chunks.end()) {
                it->second->unloadGPU();
                m_chunks.erase(it);
                anyEvicted = true;
            }
        }

        for (auto it = m_chunksWithoutMesh.begin(); it != m_chunksWithoutMesh.end(); ) {
            int dx = it->first.x - camCX;
            int dy = it->first.y - camCY;
            int dz = it->first.z - camCZ;
            if (dx * dx + dy * dy + dz * dz > evictionDist * evictionDist) {
                it = m_chunksWithoutMesh.erase(it);
                anyEvicted = true;
            } else {
                ++it;
            }
        }
    }

    if (anyEvicted) {
        rebuildRenderList();
    }
}

void World::cleanupQueuedChunks() {
    std::set<ChunkKey> existing;
    {
        std::lock_guard lock(m_chunksMutex);
        for (auto& [key, _] : m_chunks) existing.insert(key);
        for (auto& [key, _] : m_chunksWithoutMesh) existing.insert(key);
    }
    std::lock_guard lock(m_queuedMutex);
    for (auto it = m_queuedChunks.begin(); it != m_queuedChunks.end(); ) {
        if (existing.count(*it)) {
            it = m_queuedChunks.erase(it);
        } else {
            ++it;
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

void World::setLodDistance(int lod, int dist) {
    if (lod < 0 || lod >= Chunk::NUM_LODS) return;
    if (dist < 2) dist = 2;
    if (dist == m_lodDistances[lod]) return;
    m_lodDistances[lod] = dist;
    m_lastCenterX = 0x7FFFFFFF;
}

void World::getLodCounts(int counts[Chunk::NUM_LODS]) const {
    std::fill(counts, counts + Chunk::NUM_LODS, 0);
    std::lock_guard lock(m_chunksMutex);
    for (auto& [key, chunk] : m_chunks) {
        if (chunk->hasMesh()) {
            int lod = chunk->getLodLevel();
            if (lod >= 0 && lod < Chunk::NUM_LODS)
                counts[lod]++;
        }
    }
}

void World::applyTerrainParams() {
    // Drain all in-flight worker tasks before modifying the generator
    // so that no thread is reading m_terrainGen while we update it.
    m_threadPool.waitForAll();
    m_terrainGen.setParams(m_terrainParams);

    {
        std::lock_guard lock(m_chunksMutex);
        m_chunks.clear();
        m_chunksWithoutMesh.clear();
        m_renderList.clear();
    }

    {
        std::lock_guard ulock(m_uploadMutex);
        m_uploadQueue.clear();
    }

    {
        std::lock_guard qlock(m_queuedMutex);
        m_queuedChunks.clear();
    }

    m_lastCenterX = 0x7FFFFFFF;
}
