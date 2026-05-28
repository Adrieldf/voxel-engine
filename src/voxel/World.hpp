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
#include <thread>

class World {
public:
    std::unordered_map<ChunkPos, std::shared_ptr<Chunk>> chunks;
    std::mutex chunksMutex;
    
    // Noise and Seeding
    std::atomic<int> seed{1337};
    std::atomic<int> currentWorldId{0};

    // Threading
    std::unique_ptr<ThreadPool> generationThreadPool;
    std::unique_ptr<ThreadPool> meshingThreadPool;
    std::atomic<int> activeTasks{0};

    // Configuration
    int renderDistance = 16;  // Render radius in chunks (16 blocks each)
    
    // Safe queue of chunks to be uploaded on the main thread
    std::vector<std::shared_ptr<Chunk>> uploadQueue;
    std::mutex uploadQueueMutex;

    World() {
        generationThreadPool = std::make_unique<ThreadPool>(2);
        meshingThreadPool = std::make_unique<ThreadPool>(2);
    }

    ~World() {
        clearWorld();
    }

    void clearWorld() {
        TraceLog(LOG_INFO, "[WORLD] Starting clearWorld(). Active tasks: %d, Chunks size: %d", activeTasks.load(), chunks.size());
        currentWorldId++; // Invalidate all pending background tasks immediately
        
        std::lock_guard<std::mutex> lock(chunksMutex);
        for (auto& pair : chunks) {
            pair.second->unloadGPU();
        }
        chunks.clear();
        
        std::lock_guard<std::mutex> uploadLock(uploadQueueMutex);
        uploadQueue.clear();
        TraceLog(LOG_INFO, "[WORLD] Finished clearWorld() successfully.");
    }

    void recreate(int newSeed) {
        TraceLog(LOG_INFO, "[WORLD] recreate() triggered with new seed: %d", newSeed);
        clearWorld();
        seed = newSeed;
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
        int px = static_cast<int>(std::floor(playerPos.x / CHUNK_WIDTH));
        int pz = static_cast<int>(std::floor(playerPos.z / CHUNK_DEPTH));

        int worldId = currentWorldId.load();

        std::lock_guard<std::mutex> lock(chunksMutex);

        // 1. Concentric Ring Progressive Spawning within Render Distance + 1 (for boundary neighbor padding)
        int spawnedThisFrame = 0;
        const int maxSpawnsPerFrame = 12; // Prevent thread pool queue starvation

        auto spawnChunk = [&](int cx, int cz) {
            ChunkPos pos{cx, cz};
            auto chunk = std::make_shared<Chunk>(cx, cz);
            chunks[pos] = chunk;

            // Enqueue block generation in the generation thread pool
            TraceLog(LOG_INFO, "[WORLD] Enqueuing generateBlocks for Chunk (%d, %d)", cx, cz);
            activeTasks++;
            generationThreadPool->enqueue([this, chunk, worldId]() {
                if (worldId != currentWorldId.load()) {
                    activeTasks--;
                    return;
                }

                chunk->generateBlocks(seed.load());
                activeTasks--;
            });
        };

        for (int r = 0; r <= renderDistance + 1; ++r) {
            if (spawnedThisFrame >= maxSpawnsPerFrame) break;

            if (r == 0) {
                ChunkPos pos{px, pz};
                if (chunks.find(pos) == chunks.end()) {
                    spawnChunk(px, pz);
                    spawnedThisFrame++;
                }
            } else {
                // Traverse edges of the square of radius r winding outward
                // Top edge
                for (int dx = -r; dx <= r && spawnedThisFrame < maxSpawnsPerFrame; ++dx) {
                    ChunkPos pos{px + dx, pz + r};
                    if (chunks.find(pos) == chunks.end()) {
                        spawnChunk(px + dx, pz + r);
                        spawnedThisFrame++;
                    }
                }
                // Right edge
                for (int dz = r - 1; dz >= -r && spawnedThisFrame < maxSpawnsPerFrame; --dz) {
                    ChunkPos pos{px + r, pz + dz};
                    if (chunks.find(pos) == chunks.end()) {
                        spawnChunk(px + r, pz + dz);
                        spawnedThisFrame++;
                    }
                }
                // Bottom edge
                for (int dx = r - 1; dx >= -r && spawnedThisFrame < maxSpawnsPerFrame; --dx) {
                    ChunkPos pos{px + dx, pz - r};
                    if (chunks.find(pos) == chunks.end()) {
                        spawnChunk(px + dx, pz - r);
                        spawnedThisFrame++;
                    }
                }
                // Left edge
                for (int dz = -r + 1; dz <= r - 1 && spawnedThisFrame < maxSpawnsPerFrame; ++dz) {
                    ChunkPos pos{px - r, pz + dz};
                    if (chunks.find(pos) == chunks.end()) {
                        spawnChunk(px - r, pz + dz);
                        spawnedThisFrame++;
                    }
                }
            }
        }


        // 2. Schedule Meshing for generated chunks whose neighbors are also generated
        for (auto& pair : chunks) {
            auto& chunk = pair.second;
            bool wantsMesh = (!chunk->isMeshReadyCPU && !chunk->isMeshUploaded) || chunk->needsReupload.load();
            if (chunk->isGenerated && !chunk->isMeshQueued && wantsMesh) {
                
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

                    TraceLog(LOG_INFO, "[WORLD] Enqueuing generateMeshCPU for Chunk (%d, %d)", chunk->pos.x, chunk->pos.z);
                    activeTasks++;
                    meshingThreadPool->enqueue([this, chunk, nXNegPtr, nXPosPtr, nZNegPtr, nZPosPtr, 
                                         nXNegZNegPtr, nXNegZPosPtr, nXPosZNegPtr, nXPosZPosPtr, worldId]() {
                        if (worldId != currentWorldId.load()) {
                            activeTasks--;
                            return;
                        }

                        chunk->generateMeshCPU(nXNegPtr.get(), nXPosPtr.get(), nZNegPtr.get(), nZPosPtr.get(),
                                               nXNegZNegPtr.get(), nXNegZPosPtr.get(), nXPosZNegPtr.get(), nXPosZPosPtr.get());
                        chunk->isMeshQueued = false;
                        activeTasks--;
                    });
                }
            }
        }

        // 3. Unload chunks that are too far away to reclaim RAM and GPU VRAM (Amortized to prevent frame stutters)
        std::vector<ChunkPos> toUnload;
        for (auto& pair : chunks) {
            int dx = std::abs(pair.first.x - px);
            int dz = std::abs(pair.first.z - pz);
            if (dx > renderDistance + 2 || dz > renderDistance + 2) {
                toUnload.push_back(pair.first);
            }
        }
        int unloadCount = 0;
        int maxUnloadsPerFrame = 4;
        for (const auto& pos : toUnload) {
            if (unloadCount >= maxUnloadsPerFrame) break;
            TraceLog(LOG_INFO, "[WORLD] Evicting out-of-bounds Chunk (%d, %d)", pos.x, pos.z);
            chunks[pos]->unloadGPU();
            chunks.erase(pos);
            unloadCount++;
        }

        // 4. Update chunk fade-in progress (transition finishes in ~0.33s for high visual feedback)
        for (auto& pair : chunks) {
            auto& chunk = pair.second;
            if (chunk->isMeshUploaded) {
                if (chunk->fadeProgress < 1.0f) {
                    chunk->fadeProgress += dt * 3.0f;
                    if (chunk->fadeProgress > 1.0f) {
                        chunk->fadeProgress = 1.0f;
                        chunk->hasFadedIn = true;
                    }
                }
            } else {
                if (!chunk->hasFadedIn) {
                    chunk->fadeProgress = 0.0f;
                } else {
                    chunk->fadeProgress = 1.0f; // Keep fully visible if LOD scales are switching!
                }
            }
        }
    }

    void draw(Vector3 playerPos, Vector3 cameraForward, Shader voxelShader) {
        int px = static_cast<int>(std::floor(playerPos.x / CHUNK_WIDTH));
        int pz = static_cast<int>(std::floor(playerPos.z / CHUNK_DEPTH));

        int fadeLoc = GetShaderLocation(voxelShader, "fadeProgress");

        std::lock_guard<std::mutex> lock(chunksMutex);
        
        // Pass 1: Render all opaque/solid voxel submeshes first
        for (auto& pair : chunks) {
            auto& chunk = pair.second;
            int dx = std::abs(pair.first.x - px);
            int dz = std::abs(pair.first.z - pz);
            if (dx <= renderDistance && dz <= renderDistance) {
                // View Cone Bounding Sphere Culling
                Vector3 chunkCenter = {
                    (chunk->pos.x + 0.5f) * CHUNK_WIDTH,
                    128.0f,
                    (chunk->pos.z + 0.5f) * CHUNK_DEPTH
                };
                Vector3 toChunk = Vector3Subtract(chunkCenter, playerPos);
                float dist = Vector3Length(toChunk);
                if (dist > 64.0f) {
                    Vector3 dir = Vector3Scale(toChunk, 1.0f / dist);
                    float dot = Vector3DotProduct(cameraForward, dir);
                    
                    float dotThreshold = 0.2f;
                    float lookUpDown = std::abs(cameraForward.y);
                    dotThreshold -= lookUpDown * 0.15f; // lower threshold when looking up/down to keep wide angles visible
                    
                    if (dot < dotThreshold) {
                        continue; // Culled!
                    }
                }

                // If the CPU mesh is ready but not uploaded (or needs reupload), upload it on the main thread!
                if (chunk->isMeshReadyCPU && (!chunk->isMeshUploaded || chunk->needsReupload.load())) {
                    if (chunk->needsReupload.load()) {
                        chunk->unloadGPU();
                        chunk->needsReupload = false;
                    }
                    chunk->uploadGPU(voxelShader);
                }
                SetShaderValue(voxelShader, fadeLoc, &chunk->fadeProgress, SHADER_UNIFORM_FLOAT);
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
                // View Cone Bounding Sphere Culling
                Vector3 chunkCenter = {
                    (chunk->pos.x + 0.5f) * CHUNK_WIDTH,
                    128.0f,
                    (chunk->pos.z + 0.5f) * CHUNK_DEPTH
                };
                Vector3 toChunk = Vector3Subtract(chunkCenter, playerPos);
                float dist = Vector3Length(toChunk);
                if (dist > 64.0f) {
                    Vector3 dir = Vector3Scale(toChunk, 1.0f / dist);
                    float dot = Vector3DotProduct(cameraForward, dir);
                    
                    float dotThreshold = 0.2f;
                    float lookUpDown = std::abs(cameraForward.y);
                    dotThreshold -= lookUpDown * 0.15f; // lower threshold when looking up/down to keep wide angles visible
                    
                    if (dot < dotThreshold) {
                        continue; // Culled!
                    }
                }

                SetShaderValue(voxelShader, fadeLoc, &chunk->fadeProgress, SHADER_UNIFORM_FLOAT);
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
