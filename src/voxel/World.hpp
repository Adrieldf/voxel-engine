#pragma once

#include "voxel/Chunk.hpp"
#include "core/Noise.hpp"
#include "core/ThreadPool.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <cmath>

class World {
public:
    std::unordered_map<ChunkPos, std::shared_ptr<Chunk>> chunks;
    std::mutex chunksMutex;
    
    // Noise and Seeding
    Noise noise;
    std::atomic<int> seed{1337};
    std::atomic<int> currentWorldId{0};

    // Threading
    std::unique_ptr<ThreadPool> threadPool;
    std::atomic<int> activeTasks{0};

    // Configuration
    int renderDistance = 6;  // Render radius in chunks (16 blocks each)
    
    // Safe queue of chunks to be uploaded on the main thread
    std::vector<std::shared_ptr<Chunk>> uploadQueue;
    std::mutex uploadQueueMutex;

    World() {
        threadPool = std::make_unique<ThreadPool>(4);
    }

    ~World() {
        clearWorld();
    }

    void clearWorld() {
        currentWorldId++; // Invalidate all pending background tasks immediately
        
        std::lock_guard<std::mutex> lock(chunksMutex);
        for (auto& pair : chunks) {
            pair.second->unloadGPU();
        }
        chunks.clear();
        
        std::lock_guard<std::mutex> uploadLock(uploadQueueMutex);
        uploadQueue.clear();
    }

    void recreate(int newSeed) {
        seed = newSeed;
        clearWorld();
        noise.reseed(newSeed);
    }

    std::shared_ptr<Chunk> getChunk(int cx, int cz) {
        std::lock_guard<std::mutex> lock(chunksMutex);
        auto it = chunks.find({cx, cz});
        if (it != chunks.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Dynamic infinite loading around player coordinates
    void update(Vector3 playerPos, float dt) {
        (void)dt; // Unused for now

        int px = static_cast<int>(std::floor(playerPos.x / CHUNK_WIDTH));
        int pz = static_cast<int>(std::floor(playerPos.z / CHUNK_DEPTH));

        int worldId = currentWorldId.load();

        // 1. Spawning/Enqueuing Chunks within Render Distance
        for (int x = -renderDistance; x <= renderDistance; ++x) {
            for (int z = -renderDistance; z <= renderDistance; ++z) {
                int cx = px + x;
                int cz = pz + z;

                ChunkPos pos{cx, cz};
                
                std::lock_guard<std::mutex> lock(chunksMutex);
                if (chunks.find(pos) == chunks.end()) {
                    auto chunk = std::make_shared<Chunk>(cx, cz);
                    chunks[pos] = chunk;

                    // Enqueue block generation in the thread pool
                    activeTasks++;
                    threadPool->enqueue([this, chunk, worldId]() {
                        if (worldId != currentWorldId.load()) {
                            activeTasks--;
                            return;
                        }

                        chunk->generateBlocks(noise, seed.load());
                        activeTasks--;
                    });
                }
            }
        }

        // 2. Schedule Meshing for generated chunks whose neighbors are also generated
        {
            std::lock_guard<std::mutex> lock(chunksMutex);
            for (auto& pair : chunks) {
                auto& chunk = pair.second;
                if (chunk->isGenerated && !chunk->isMeshQueued && !chunk->isMeshReadyCPU && !chunk->isMeshUploaded) {
                    
                    // Retrieve neighbors (4 cardinals + 4 diagonals)
                    auto nXNeg = chunks.find({chunk->pos.x - 1, chunk->pos.z});
                    auto nXPos = chunks.find({chunk->pos.x + 1, chunk->pos.z});
                    auto nZNeg = chunks.find({chunk->pos.x, chunk->pos.z - 1});
                    auto nZPos = chunks.find({chunk->pos.x, chunk->pos.z + 1});
                    auto nXNegZNeg = chunks.find({chunk->pos.x - 1, chunk->pos.z - 1});
                    auto nXNegZPos = chunks.find({chunk->pos.x - 1, chunk->pos.z + 1});
                    auto nXPosZNeg = chunks.find({chunk->pos.x + 1, chunk->pos.z - 1});
                    auto nXPosZPos = chunks.find({chunk->pos.x + 1, chunk->pos.z + 1});

                    bool neighborsReady = (nXNeg != chunks.end() && nXNeg->second->isGenerated) &&
                                          (nXPos != chunks.end() && nXPos->second->isGenerated) &&
                                          (nZNeg != chunks.end() && nZNeg->second->isGenerated) &&
                                          (nZPos != chunks.end() && nZPos->second->isGenerated) &&
                                          (nXNegZNeg != chunks.end() && nXNegZNeg->second->isGenerated) &&
                                          (nXNegZPos != chunks.end() && nXNegZPos->second->isGenerated) &&
                                          (nXPosZNeg != chunks.end() && nXPosZNeg->second->isGenerated) &&
                                          (nXPosZPos != chunks.end() && nXPosZPos->second->isGenerated);

                    if (neighborsReady) {
                        chunk->isMeshQueued = true;

                        // Safely capture neighbors as shared_ptrs so they cannot be deleted during background meshing
                        auto nXNegPtr = nXNeg->second;
                        auto nXPosPtr = nXPos->second;
                        auto nZNegPtr = nZNeg->second;
                        auto nZPosPtr = nZPos->second;
                        auto nXNegZNegPtr = nXNegZNeg->second;
                        auto nXNegZPosPtr = nXNegZPos->second;
                        auto nXPosZNegPtr = nXPosZNeg->second;
                        auto nXPosZPosPtr = nXPosZPos->second;

                        activeTasks++;
                        threadPool->enqueue([this, chunk, nXNegPtr, nXPosPtr, nZNegPtr, nZPosPtr, 
                                             nXNegZNegPtr, nXNegZPosPtr, nXPosZNegPtr, nXPosZPosPtr, worldId]() {
                            if (worldId != currentWorldId.load()) {
                                activeTasks--;
                                return;
                            }

                            chunk->generateMeshCPU(nXNegPtr.get(), nXPosPtr.get(), nZNegPtr.get(), nZPosPtr.get(),
                                                   nXNegZNegPtr.get(), nXNegZPosPtr.get(), nXPosZNegPtr.get(), nXPosZPosPtr.get());
                            activeTasks--;
                        });
                    }
                }
            }
        }

        // 3. Unload chunks that are too far away to reclaim RAM and GPU VRAM
        std::vector<ChunkPos> toUnload;
        {
            std::lock_guard<std::mutex> lock(chunksMutex);
            for (auto& pair : chunks) {
                int dx = std::abs(pair.first.x - px);
                int dz = std::abs(pair.first.z - pz);
                if (dx > renderDistance + 2 || dz > renderDistance + 2) {
                    toUnload.push_back(pair.first);
                }
            }
            for (const auto& pos : toUnload) {
                chunks[pos]->unloadGPU();
                chunks.erase(pos);
            }
        }
    }

    void draw(Vector3 playerPos, Shader voxelShader) {
        int px = static_cast<int>(std::floor(playerPos.x / CHUNK_WIDTH));
        int pz = static_cast<int>(std::floor(playerPos.z / CHUNK_DEPTH));

        std::lock_guard<std::mutex> lock(chunksMutex);
        
        // Pass 1: Render all opaque/solid voxel submeshes first
        for (auto& pair : chunks) {
            auto& chunk = pair.second;
            int dx = std::abs(pair.first.x - px);
            int dz = std::abs(pair.first.z - pz);
            if (dx <= renderDistance && dz <= renderDistance) {
                // If the CPU mesh is ready but not uploaded, upload it on the main thread!
                if (chunk->isMeshReadyCPU && !chunk->isMeshUploaded) {
                    chunk->uploadGPU(voxelShader);
                }
                chunk->draw();
            }
        }

        // Pass 2: Render all transparent voxel submeshes (water) with depth writing disabled
        rlDisableDepthMask();
        for (auto& pair : chunks) {
            auto& chunk = pair.second;
            int dx = std::abs(pair.first.x - px);
            int dz = std::abs(pair.first.z - pz);
            if (dx <= renderDistance && dz <= renderDistance) {
                chunk->drawTransparent();
            }
        }
        rlEnableDepthMask();
    }

    int getChunkCount() {
        std::lock_guard<std::mutex> lock(chunksMutex);
        return static_cast<int>(chunks.size());
    }
};
