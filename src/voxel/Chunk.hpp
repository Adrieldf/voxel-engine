#pragma once

#include "voxel/Voxel.hpp"
#include "core/Noise.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "external/glad.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstring>

// Hardware-accelerated trailing zero count
#ifdef _MSC_VER
#include <intrin.h>
inline uint32_t countTrailingZeros(uint32_t mask) {
    unsigned long index;
    return _BitScanForward(&index, mask) ? index : 32;
}
#else
inline uint32_t countTrailingZeros(uint32_t mask) {
    return mask ? __builtin_ctz(mask) : 32;
}
#endif

// Chunk dimensions: 32 x 256 x 32
static constexpr int CHUNK_WIDTH = 32;
static constexpr int CHUNK_HEIGHT = 256;
static constexpr int CHUNK_DEPTH = 32;
static constexpr int CHUNK_SIZE = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH;

struct ChunkPos {
    int x;
    int z;

    bool operator==(const ChunkPos& other) const {
        return x == other.x && z == other.z;
    }
};

namespace std {
    template <>
    struct hash<ChunkPos> {
        size_t operator()(const ChunkPos& pos) const {
            return (hash<int>()(pos.x) ^ (hash<int>()(pos.z) << 1));
        }
    };
}

class Chunk {
public:
    ChunkPos pos;
    BlockType blocks[CHUNK_SIZE];
    
    // Threading states
    std::atomic<bool> isGenerated{false};
    std::atomic<bool> isMeshQueued{false};
    std::atomic<bool> isMeshReadyCPU{false};
    std::atomic<bool> isMeshUploaded{false};
    
    struct SubMesh {
        std::vector<uint32_t> packedVertices;
        std::vector<unsigned short> indices;
    };
    std::vector<SubMesh> subMeshes;
    std::vector<SubMesh> subMeshesTransparent;
    
    Model model;
    Model modelTransparent;
    std::mutex meshMutex;

    // Fast allocation buffers for thread-safe high-throughput meshing
    struct MeshingBuffers {
        uint32_t masks_neg[512][256];
        uint32_t masks_pos[512][256];
        uint16_t active_neg[512];
        uint16_t active_pos[512];
        bool is_active_neg[512];
        bool is_active_pos[512];
        uint16_t count_neg = 0;
        uint16_t count_pos = 0;
    } mBuf;

    Chunk(int cx, int cz) : pos{cx, cz} {
        std::fill(blocks, blocks + CHUNK_SIZE, BLOCK_AIR);
        model.meshCount = 0;
        model.meshes = nullptr;
        model.materials = nullptr;
        model.meshMaterial = nullptr;
        model.materialCount = 0;
        modelTransparent.meshCount = 0;
        modelTransparent.meshes = nullptr;
        modelTransparent.materials = nullptr;
        modelTransparent.meshMaterial = nullptr;
        modelTransparent.materialCount = 0;
        std::fill(&mBuf.is_active_neg[0], &mBuf.is_active_neg[512], false);
        std::fill(&mBuf.is_active_pos[0], &mBuf.is_active_pos[512], false);
        std::memset(mBuf.masks_neg, 0, sizeof(mBuf.masks_neg));
        std::memset(mBuf.masks_pos, 0, sizeof(mBuf.masks_pos));
        subMeshes.reserve(4);
        subMeshesTransparent.reserve(4);
    }

    ~Chunk() {
        unloadGPU();
    }

    void unloadGPU() {
        if (isMeshUploaded) {
            UnloadModel(model);
            if (modelTransparent.meshCount > 0) {
                UnloadModel(modelTransparent);
            }
            isMeshUploaded = false;
            model.meshCount = 0;
            model.meshes = nullptr;
            model.materials = nullptr;
            model.meshMaterial = nullptr;
            model.materialCount = 0;
            modelTransparent.meshCount = 0;
            modelTransparent.meshes = nullptr;
            modelTransparent.materials = nullptr;
            modelTransparent.meshMaterial = nullptr;
            modelTransparent.materialCount = 0;
        }
    }

    inline int getIndex(int x, int y, int z) const {
        return x + (z * CHUNK_WIDTH) + (y * CHUNK_WIDTH * CHUNK_DEPTH);
    }

    BlockType getBlock(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_WIDTH || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_DEPTH) {
            return BLOCK_AIR;
        }
        return blocks[getIndex(x, y, z)];
    }

    void setBlock(int x, int y, int z, BlockType type) {
        if (x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < CHUNK_HEIGHT && z >= 0 && z < CHUNK_DEPTH) {
            blocks[getIndex(x, y, z)] = type;
        }
    }

    inline uint16_t getVoxel(int x, int y, int z,
                             const Chunk* neighborXNeg, const Chunk* neighborXPos,
                             const Chunk* neighborZNeg, const Chunk* neighborZPos,
                             const Chunk* neighborXNegZNeg, const Chunk* neighborXNegZPos,
                             const Chunk* neighborXPosZNeg, const Chunk* neighborXPosZPos) const 
    {
        if (y < 0 || y >= CHUNK_HEIGHT) return BLOCK_AIR;

        if (x < 0) {
            if (z < 0) {
                return neighborXNegZNeg ? neighborXNegZNeg->blocks[31 + (31 * 32) + (y * 1024)] : BLOCK_AIR;
            }
            if (z >= CHUNK_DEPTH) {
                return neighborXNegZPos ? neighborXNegZPos->blocks[31 + (0 * 32) + (y * 1024)] : BLOCK_AIR;
            }
            return neighborXNeg ? neighborXNeg->blocks[31 + (z * 32) + (y * 1024)] : BLOCK_AIR;
        }
        if (x >= CHUNK_WIDTH) {
            if (z < 0) {
                return neighborXPosZNeg ? neighborXPosZNeg->blocks[0 + (31 * 32) + (y * 1024)] : BLOCK_AIR;
            }
            if (z >= CHUNK_DEPTH) {
                return neighborXPosZPos ? neighborXPosZPos->blocks[0 + (0 * 32) + (y * 1024)] : BLOCK_AIR;
            }
            return neighborXPos ? neighborXPos->blocks[0 + (z * 32) + (y * 1024)] : BLOCK_AIR;
        }
        if (z < 0) {
            return neighborZNeg ? neighborZNeg->blocks[x + (31 * 32) + (y * 1024)] : BLOCK_AIR;
        }
        if (z >= CHUNK_DEPTH) {
            return neighborZPos ? neighborZPos->blocks[x + (0 * 32) + (y * 1024)] : BLOCK_AIR;
        }

        return blocks[x + (z * 32) + (y * 1024)];
    }

    static inline float GetRandomValue(int x, int z, int seed) {
        unsigned int h = x * 73856093 ^ z * 19349663 ^ seed;
        h = (h ^ 61) ^ (h >> 16);
        h += (h << 3);
        h ^= (h >> 4);
        h *= 0x27d4eb2d;
        h ^= (h >> 15);
        return static_cast<float>(h) / 4294967295.0f;
    }

    // Procedural Block Generation using scaled Perlin Noise up to 256 height
    void generateBlocks(const Noise& noise, int seed) {
        int worldXOffset = pos.x * CHUNK_WIDTH;
        int worldZOffset = pos.z * CHUNK_DEPTH;

        for (int x = 0; x < CHUNK_WIDTH; ++x) {
            for (int z = 0; z < CHUNK_DEPTH; ++z) {
                int absX = worldXOffset + x;
                int absZ = worldZOffset + z;

                double scale = 0.003;
                double n = noise.noiseOctaves(absX * scale + seed * 0.1, absZ * scale + seed * 0.1, 4, 0.5, 2.0);
                
                // Scale surface height beautifully within grand range [70, 198]
                int surfaceHeight = static_cast<int>(130 + n * 64);
                if (surfaceHeight < 1) surfaceHeight = 1;
                if (surfaceHeight >= CHUNK_HEIGHT) surfaceHeight = CHUNK_HEIGHT - 1;

                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    int index = getIndex(x, y, z);
                    if (y > surfaceHeight) {
                        blocks[index] = (y <= 110) ? BLOCK_WATER : BLOCK_AIR;
                    } else if (y == surfaceHeight) {
                        if (y <= 112) {
                            blocks[index] = BLOCK_SAND;
                        } else if (y >= 170) {
                            blocks[index] = BLOCK_SNOW;
                        } else {
                            blocks[index] = BLOCK_GRASS;
                        }
                    } else if (y > surfaceHeight - 4) {
                        blocks[index] = (y <= 112) ? BLOCK_SAND : BLOCK_DIRT;
                    } else {
                        blocks[index] = BLOCK_STONE;
                    }
                }

                // Procedural trees in fertile grassland altitudes
                if (surfaceHeight > 115 && surfaceHeight < 165) {
                    float randVal = GetRandomValue(absX, absZ, seed);
                    if (randVal < 0.015f) {
                        int trunkHeight = 5 + (static_cast<int>(randVal * 1000.0f) % 3);
                        int treeBaseY = surfaceHeight + 1;
                        
                        if (getBlock(x, surfaceHeight, z) == BLOCK_GRASS) {
                            for (int ty = 0; ty < trunkHeight; ++ty) {
                                int yPos = treeBaseY + ty;
                                if (yPos < CHUNK_HEIGHT) {
                                    setBlock(x, yPos, z, BLOCK_WOOD);
                                }
                            }
                            
                            int leafCenterY = treeBaseY + trunkHeight - 1;
                            for (int lx = -2; lx <= 2; ++lx) {
                                for (int lz = -2; lz <= 2; ++lz) {
                                    for (int ly = 0; ly <= 2; ++ly) {
                                        int absLX = x + lx;
                                        int absLZ = z + lz;
                                        int absLY = leafCenterY + ly;

                                        if (std::abs(lx) == 2 && std::abs(lz) == 2 && ly != 0) continue;
                                        
                                        if (absLX >= 0 && absLX < CHUNK_WIDTH && absLZ >= 0 && absLZ < CHUNK_DEPTH && absLY < CHUNK_HEIGHT) {
                                            if (getBlock(absLX, absLY, absLZ) == BLOCK_AIR) {
                                                setBlock(absLX, absLY, absLZ, BLOCK_LEAVES);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        isGenerated = true;
    }

    // Pack vertex attributes into a 32-bit uint (6-9-6 position mapping layout + 4-bit texture + 2-bit AO)
    inline uint32_t packVertex(uint32_t x, uint32_t y, uint32_t z, uint32_t normal, uint32_t corner, uint32_t textureId, uint32_t ao) const {
        return (x & 0x3FU) |
               ((y & 0x1FFU) << 6) |
               ((z & 0x3FU) << 15) |
               ((normal & 0x7U) << 21) |
               ((corner & 0x3U) << 24) |
               ((textureId & 0xFU) << 26) |
               ((ao & 0x3U) << 30);
    }

    inline void addQuad(
        uint32_t x0, uint32_t y0, uint32_t z0,
        uint32_t x1, uint32_t y1, uint32_t z1,
        uint32_t x2, uint32_t y2, uint32_t z2,
        uint32_t x3, uint32_t y3, uint32_t z3,
        uint32_t normal, uint32_t textureId,
        uint32_t ao0, uint32_t ao1, uint32_t ao2, uint32_t ao3
    ) {
        bool isTrans = BLOCK_TABLE[textureId].isTransparent;
        auto& targetList = isTrans ? subMeshesTransparent : subMeshes;

        if (targetList.empty() || targetList.back().packedVertices.size() >= 65000) {
            SubMesh newSub;
            newSub.packedVertices.reserve(16384);
            newSub.indices.reserve(24576);
            targetList.push_back(std::move(newSub));
        }

        auto& currentMesh = targetList.back();
        uint32_t startIndex = static_cast<uint32_t>(currentMesh.packedVertices.size());

        currentMesh.packedVertices.push_back(packVertex(x0, y0, z0, normal, 0, textureId, ao0));
        currentMesh.packedVertices.push_back(packVertex(x1, y1, z1, normal, 1, textureId, ao1));
        currentMesh.packedVertices.push_back(packVertex(x2, y2, z2, normal, 2, textureId, ao2));
        currentMesh.packedVertices.push_back(packVertex(x3, y3, z3, normal, 3, textureId, ao3));

        // Anisotropic lighting diagonal flip fix (from 0fps.net)
        if (ao0 + ao2 > ao1 + ao3) {
            // Split along the 0-2 diagonal
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 0));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 1));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 2));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 0));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 2));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 3));
        } else {
            // Split along the 1-3 diagonal
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 0));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 1));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 3));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 1));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 2));
            currentMesh.indices.push_back(static_cast<unsigned short>(startIndex + 3));
        }
    }

    // Highly optimized Binary Greedy Meshing system using bitmasks and fast bit scanning
    void generateMeshCPU(const Chunk* neighborXNeg, const Chunk* neighborXPos, 
                          const Chunk* neighborZNeg, const Chunk* neighborZPos,
                          const Chunk* neighborXNegZNeg, const Chunk* neighborXNegZPos,
                          const Chunk* neighborXPosZNeg, const Chunk* neighborXPosZPos) 
    {
        std::lock_guard<std::mutex> lock(meshMutex);
        subMeshes.clear();
        subMeshesTransparent.clear();

        auto isVoxelSolid = [&](int vx, int vy, int vz) -> bool {
            uint16_t type = getVoxel(vx, vy, vz, 
                                     neighborXNeg, neighborXPos, 
                                     neighborZNeg, neighborZPos,
                                     neighborXNegZNeg, neighborXNegZPos,
                                     neighborXPosZNeg, neighborXPosZPos);
            return BLOCK_TABLE[type].isSolid;
        };

        auto getAO = [&](bool side1, bool side2, bool corner) -> uint32_t {
            if (side1 && side2) return 0;
            return 3 - (side1 + side2 + corner);
        };

        auto getHorizontalAO = [&](int vx, int vy, int vz, int x_origin, int z_origin) -> uint32_t {
            int vx_own = (vx == x_origin) ? vx : vx - 1;
            int vz_own = (vz == z_origin) ? vz : vz - 1;
            int nx = (vx == x_origin) ? vx - 1 : vx;
            int nz = (vz == z_origin) ? vz - 1 : vz;
            bool s1 = isVoxelSolid(vx_own, vy, nz);
            bool s2 = isVoxelSolid(nx, vy, vz_own);
            bool c = isVoxelSolid(nx, vy, nz);
            return getAO(s1, s2, c);
        };

        auto getVerticalAO_X = [&](int vx, int vy, int vz, int y_origin, int z_origin) -> uint32_t {
            int vy_own = (vy == y_origin) ? vy : vy - 1;
            int vz_own = (vz == z_origin) ? vz : vz - 1;
            int ny = (vy == y_origin) ? vy - 1 : vy;
            int nz = (vz == z_origin) ? vz - 1 : vz;
            bool s1 = isVoxelSolid(vx, vy_own, nz);
            bool s2 = isVoxelSolid(vx, ny, vz_own);
            bool c = isVoxelSolid(vx, ny, nz);
            return getAO(s1, s2, c);
        };

        auto getVerticalAO_Z = [&](int vx, int vy, int vz, int x_origin, int y_origin) -> uint32_t {
            int vx_own = (vx == x_origin) ? vx : vx - 1;
            int vy_own = (vy == y_origin) ? vy : vy - 1;
            int nx = (vx == x_origin) ? vx - 1 : vx;
            int ny = (vy == y_origin) ? vy - 1 : vy;
            bool s1 = isVoxelSolid(nx, vy_own, vz);
            bool s2 = isVoxelSolid(vx_own, ny, vz);
            bool c = isVoxelSolid(nx, ny, vz);
            return getAO(s1, s2, c);
        };

        auto shouldRenderFace = [](BlockType block, BlockType neighborBlock) -> bool {
            if (neighborBlock == BLOCK_AIR) return true;
            
            BlockInfo info = BLOCK_TABLE[block];
            BlockInfo neighborInfo = BLOCK_TABLE[neighborBlock];
            
            if (info.isSolid && neighborInfo.isTransparent) return true;
            if (!info.isSolid && neighborInfo.isTransparent && block != neighborBlock) return true;
            
            return false;
        };

        // ==========================================
        // 1. SWEEP AXIS 1: Y-AXIS (Bottom & Top Faces)
        // ==========================================
        for (int y = 0; y <= CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_DEPTH; ++z) {
                for (int x = 0; x < CHUNK_WIDTH; ++x) {
                    uint16_t vox_curr = getVoxel(x, y, z, 
                                                 neighborXNeg, neighborXPos, 
                                                 neighborZNeg, neighborZPos,
                                                 neighborXNegZNeg, neighborXNegZPos,
                                                 neighborXPosZNeg, neighborXPosZPos);
                    uint16_t vox_prev = getVoxel(x, y - 1, z, 
                                                 neighborXNeg, neighborXPos, 
                                                 neighborZNeg, neighborZPos,
                                                 neighborXNegZNeg, neighborXNegZPos,
                                                 neighborXPosZNeg, neighborXPosZPos);

                    // -Y Face (Bottom)
                    if (vox_curr > 0 && shouldRenderFace(static_cast<BlockType>(vox_curr), static_cast<BlockType>(vox_prev))) {
                        uint16_t id = vox_curr & 511;
                        if (!mBuf.is_active_neg[id]) {
                            mBuf.is_active_neg[id] = true;
                            mBuf.active_neg[mBuf.count_neg++] = id;
                        }
                        mBuf.masks_neg[id][z] |= (1U << x);
                    }

                    // +Y Face (Top)
                    if (vox_prev > 0 && shouldRenderFace(static_cast<BlockType>(vox_prev), static_cast<BlockType>(vox_curr))) {
                        uint16_t id = vox_prev & 511;
                        if (!mBuf.is_active_pos[id]) {
                            mBuf.is_active_pos[id] = true;
                            mBuf.active_pos[mBuf.count_pos++] = id;
                        }
                        mBuf.masks_pos[id][z] |= (1U << x);
                    }
                }
            }

            // Greedy mesh -Y
            for (int i = 0; i < mBuf.count_neg; ++i) {
                uint16_t id = mBuf.active_neg[i];
                uint32_t* mask = mBuf.masks_neg[id];
                for (int z = 0; z < CHUNK_DEPTH; ++z) {
                    while (mask[z] != 0) {
                        int x = countTrailingZeros(mask[z]);
                        int width = 1;
                        int height = 1;
                        uint32_t run_mask = 1U << x;

                        uint32_t ao0 = getHorizontalAO(x,         y - 1, z,          x, z);
                        uint32_t ao1 = getHorizontalAO(x + width, y - 1, z,          x, z);
                        uint32_t ao2 = getHorizontalAO(x + width, y - 1, z + height, x, z);
                        uint32_t ao3 = getHorizontalAO(x,         y - 1, z + height, x, z);
                        addQuad(
                            x,         y, z,
                            x + width, y, z,
                            x + width, y, z + height,
                            x,         y, z + height,
                            2, id,
                            ao0, ao1, ao2, ao3
                        );
                        mask[z] &= ~run_mask;
                    }
                }
                mBuf.is_active_neg[id] = false;
            }
            buffersResetRow(mBuf.active_neg, mBuf.count_neg, mBuf.masks_neg, CHUNK_DEPTH);
            mBuf.count_neg = 0;

            // Greedy mesh +Y
            for (int i = 0; i < mBuf.count_pos; ++i) {
                uint16_t id = mBuf.active_pos[i];
                uint32_t* mask = mBuf.masks_pos[id];
                for (int z = 0; z < CHUNK_DEPTH; ++z) {
                    while (mask[z] != 0) {
                        int x = countTrailingZeros(mask[z]);
                        int width = 1;
                        int height = 1;
                        uint32_t run_mask = 1U << x;

                        uint32_t ao0 = getHorizontalAO(x,         y, z + height, x, z);
                        uint32_t ao1 = getHorizontalAO(x + width, y, z + height, x, z);
                        uint32_t ao2 = getHorizontalAO(x + width, y, z,          x, z);
                        uint32_t ao3 = getHorizontalAO(x,         y, z,          x, z);
                        addQuad(
                            x,         y, z + height,
                            x + width, y, z + height,
                            x + width, y, z,
                            x,         y, z,
                            3, id,
                            ao0, ao1, ao2, ao3
                        );
                        mask[z] &= ~run_mask;
                    }
                }
                mBuf.is_active_pos[id] = false;
            }
            buffersResetRow(mBuf.active_pos, mBuf.count_pos, mBuf.masks_pos, CHUNK_DEPTH);
            mBuf.count_pos = 0;
        }

        // ==========================================
        // 2. SWEEP AXIS 0: X-AXIS (Left & Right Faces)
        // ==========================================
        for (int x = 0; x <= CHUNK_WIDTH; ++x) {
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                for (int z = 0; z < CHUNK_DEPTH; ++z) {
                    uint16_t vox_curr = getVoxel(x, y, z, 
                                                 neighborXNeg, neighborXPos, 
                                                 neighborZNeg, neighborZPos,
                                                 neighborXNegZNeg, neighborXNegZPos,
                                                 neighborXPosZNeg, neighborXPosZPos);
                    uint16_t vox_prev = getVoxel(x - 1, y, z, 
                                                 neighborXNeg, neighborXPos, 
                                                 neighborZNeg, neighborZPos,
                                                 neighborXNegZNeg, neighborXNegZPos,
                                                 neighborXPosZNeg, neighborXPosZPos);

                    // -X Face
                    if (vox_curr > 0 && shouldRenderFace(static_cast<BlockType>(vox_curr), static_cast<BlockType>(vox_prev))) {
                        uint16_t id = vox_curr & 511;
                        if (!mBuf.is_active_neg[id]) {
                            mBuf.is_active_neg[id] = true;
                            mBuf.active_neg[mBuf.count_neg++] = id;
                        }
                        mBuf.masks_neg[id][y] |= (1U << z);
                    }

                    // +X Face
                    if (vox_prev > 0 && shouldRenderFace(static_cast<BlockType>(vox_prev), static_cast<BlockType>(vox_curr))) {
                        uint16_t id = vox_prev & 511;
                        if (!mBuf.is_active_pos[id]) {
                            mBuf.is_active_pos[id] = true;
                            mBuf.active_pos[mBuf.count_pos++] = id;
                        }
                        mBuf.masks_pos[id][y] |= (1U << z);
                    }
                }
            }

            // Greedy mesh -X
            for (int i = 0; i < mBuf.count_neg; ++i) {
                uint16_t id = mBuf.active_neg[i];
                uint32_t* mask = mBuf.masks_neg[id];
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    while (mask[y] != 0) {
                        int z = countTrailingZeros(mask[y]);
                        int width = 1;
                        int height = 1;
                        uint32_t run_mask = 1U << z;

                        uint32_t ao0 = getVerticalAO_X(x - 1, y,              z,              y, z);
                        uint32_t ao1 = getVerticalAO_X(x - 1, y,              z + width,      y, z);
                        uint32_t ao2 = getVerticalAO_X(x - 1, y + height,     z + width,      y, z);
                        uint32_t ao3 = getVerticalAO_X(x - 1, y + height,     z,              y, z);
                        addQuad(
                            x, y,          z,
                            x, y,          z + width,
                            x, y + height, z + width,
                            x, y + height, z,
                            0, id,
                            ao0, ao1, ao2, ao3
                        );
                        mask[y] &= ~run_mask;
                    }
                }
                mBuf.is_active_neg[id] = false;
            }
            buffersResetRow(mBuf.active_neg, mBuf.count_neg, mBuf.masks_neg, CHUNK_HEIGHT);
            mBuf.count_neg = 0;

            // Greedy mesh +X
            for (int i = 0; i < mBuf.count_pos; ++i) {
                uint16_t id = mBuf.active_pos[i];
                uint32_t* mask = mBuf.masks_pos[id];
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    while (mask[y] != 0) {
                        int z = countTrailingZeros(mask[y]);
                        int width = 1;
                        int height = 1;
                        uint32_t run_mask = 1U << z;

                        uint32_t ao0 = getVerticalAO_X(x, y,              z + width,      y, z);
                        uint32_t ao1 = getVerticalAO_X(x, y,              z,              y, z);
                        uint32_t ao2 = getVerticalAO_X(x, y + height,     z,              y, z);
                        uint32_t ao3 = getVerticalAO_X(x, y + height,     z + width,      y, z);
                        addQuad(
                            x, y,          z + width,
                            x, y,          z,
                            x, y + height, z,
                            x, y + height, z + width,
                            1, id,
                            ao0, ao1, ao2, ao3
                        );
                        mask[y] &= ~run_mask;
                    }
                }
                mBuf.is_active_pos[id] = false;
            }
            buffersResetRow(mBuf.active_pos, mBuf.count_pos, mBuf.masks_pos, CHUNK_HEIGHT);
            mBuf.count_pos = 0;
        }

        // ==========================================
        // 3. SWEEP AXIS 2: Z-AXIS (North & South Faces)
        // ==========================================
        for (int z = 0; z <= CHUNK_DEPTH; ++z) {
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                for (int x = 0; x < CHUNK_WIDTH; ++x) {
                    uint16_t vox_curr = getVoxel(x, y, z, 
                                                 neighborXNeg, neighborXPos, 
                                                 neighborZNeg, neighborZPos,
                                                 neighborXNegZNeg, neighborXNegZPos,
                                                 neighborXPosZNeg, neighborXPosZPos);
                    uint16_t vox_prev = getVoxel(x, y, z - 1, 
                                                 neighborXNeg, neighborXPos, 
                                                 neighborZNeg, neighborZPos,
                                                 neighborXNegZNeg, neighborXNegZPos,
                                                 neighborXPosZNeg, neighborXPosZPos);

                    // -Z Face
                    if (vox_curr > 0 && shouldRenderFace(static_cast<BlockType>(vox_curr), static_cast<BlockType>(vox_prev))) {
                        uint16_t id = vox_curr & 511;
                        if (!mBuf.is_active_neg[id]) {
                            mBuf.is_active_neg[id] = true;
                            mBuf.active_neg[mBuf.count_neg++] = id;
                        }
                        mBuf.masks_neg[id][y] |= (1U << x);
                    }

                    // +Z Face
                    if (vox_prev > 0 && shouldRenderFace(static_cast<BlockType>(vox_prev), static_cast<BlockType>(vox_curr))) {
                        uint16_t id = vox_prev & 511;
                        if (!mBuf.is_active_pos[id]) {
                            mBuf.is_active_pos[id] = true;
                            mBuf.active_pos[mBuf.count_pos++] = id;
                        }
                        mBuf.masks_pos[id][y] |= (1U << x);
                    }
                }
            }

            // Greedy mesh -Z
            for (int i = 0; i < mBuf.count_neg; ++i) {
                uint16_t id = mBuf.active_neg[i];
                uint32_t* mask = mBuf.masks_neg[id];
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    while (mask[y] != 0) {
                        int x = countTrailingZeros(mask[y]);
                        int width = 1;
                        int height = 1;
                        uint32_t run_mask = 1U << x;

                        uint32_t ao0 = getVerticalAO_Z(x + width, y,              z - 1, x, y);
                        uint32_t ao1 = getVerticalAO_Z(x,         y,              z - 1, x, y);
                        uint32_t ao2 = getVerticalAO_Z(x,         y + height,     z - 1, x, y);
                        uint32_t ao3 = getVerticalAO_Z(x + width, y + height,     z - 1, x, y);
                        addQuad(
                            x + width, y,          z,
                            x,         y,          z,
                            x,         y + height, z,
                            x + width, y + height, z,
                            4, id,
                            ao0, ao1, ao2, ao3
                        );
                        mask[y] &= ~run_mask;
                    }
                }
                mBuf.is_active_neg[id] = false;
            }
            buffersResetRow(mBuf.active_neg, mBuf.count_neg, mBuf.masks_neg, CHUNK_HEIGHT);
            mBuf.count_neg = 0;

            // Greedy mesh +Z
            for (int i = 0; i < mBuf.count_pos; ++i) {
                uint16_t id = mBuf.active_pos[i];
                uint32_t* mask = mBuf.masks_pos[id];
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    while (mask[y] != 0) {
                        int x = countTrailingZeros(mask[y]);
                        int width = 1;
                        int height = 1;
                        uint32_t run_mask = 1U << x;

                        uint32_t ao0 = getVerticalAO_Z(x,         y,              z, x, y);
                        uint32_t ao1 = getVerticalAO_Z(x + width, y,              z, x, y);
                        uint32_t ao2 = getVerticalAO_Z(x + width, y + height,     z, x, y);
                        uint32_t ao3 = getVerticalAO_Z(x,         y + height,     z, x, y);
                        addQuad(
                            x,         y,          z,
                            x + width, y,          z,
                            x + width, y + height, z,
                            x,         y + height, z,
                            5, id,
                            ao0, ao1, ao2, ao3
                        );
                        mask[y] &= ~run_mask;
                    }
                }
                mBuf.is_active_pos[id] = false;
            }
            buffersResetRow(mBuf.active_pos, mBuf.count_pos, mBuf.masks_pos, CHUNK_HEIGHT);
            mBuf.count_pos = 0;
        }

        isMeshReadyCPU = true;
    }

    // Fast selective bitmask rows reset to avoid general 512KB memset cleanups
    inline void buffersResetRow(const uint16_t* active_list, uint16_t count, uint32_t masks[512][256], int max_rows) {
        for (int i = 0; i < count; ++i) {
            uint16_t id = active_list[i];
            for (int r = 0; r < max_rows; ++r) {
                masks[id][r] = 0;
            }
        }
    }

    // Main thread mesh upload using OpenGL glVertexAttribIPointer for custom bit-packed buffers
    void uploadGPU(Shader customShader) {
        std::lock_guard<std::mutex> lock(meshMutex);
        if (!isMeshReadyCPU || isMeshUploaded) return;

        auto uploadModelHelper = [&](Model& targetModel, std::vector<SubMesh>& targetList) {
            if (targetList.empty()) {
                targetModel.meshCount = 0;
                targetModel.meshes = nullptr;
                targetModel.materials = nullptr;
                targetModel.meshMaterial = nullptr;
                targetModel.materialCount = 0;
                return;
            }

            int meshCount = static_cast<int>(targetList.size());
            targetModel.transform = MatrixTranslate(static_cast<float>(pos.x * CHUNK_WIDTH), 0.0f, static_cast<float>(pos.z * CHUNK_DEPTH));
            targetModel.meshCount = meshCount;
            targetModel.meshes = (Mesh*)RL_MALLOC(meshCount * sizeof(Mesh));
            targetModel.materialCount = 1;
            targetModel.materials = (Material*)RL_MALLOC(sizeof(Material));
            targetModel.materials[0] = LoadMaterialDefault();
            targetModel.materials[0].shader = customShader;

            targetModel.meshMaterial = (int*)RL_MALLOC(meshCount * sizeof(int));
            for (int i = 0; i < meshCount; ++i) {
                targetModel.meshMaterial[i] = 0;
            }

            for (int i = 0; i < meshCount; ++i) {
                auto& sub = targetList[i];
                Mesh& mesh = targetModel.meshes[i];
                std::memset(&mesh, 0, sizeof(Mesh));

                mesh.vertexCount = static_cast<int>(sub.packedVertices.size());
                mesh.triangleCount = static_cast<int>(sub.indices.size() / 3);

                mesh.indices = (unsigned short*)RL_MALLOC(sub.indices.size() * sizeof(unsigned short));
                std::memcpy(mesh.indices, sub.indices.data(), sub.indices.size() * sizeof(unsigned short));

                mesh.vaoId = rlLoadVertexArray();
                rlEnableVertexArray(mesh.vaoId);

                mesh.vboId = (unsigned int*)RL_MALLOC(2 * sizeof(unsigned int));
                mesh.vboId[0] = rlLoadVertexBuffer(sub.packedVertices.data(), static_cast<int>(sub.packedVertices.size() * sizeof(uint32_t)), false);
                rlEnableVertexAttribute(0);
                glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, 0, (void*)0);

                mesh.vboId[1] = rlLoadVertexBufferElement(sub.indices.data(), static_cast<int>(sub.indices.size() * sizeof(unsigned short)), false);

                rlDisableVertexArray();
            }
        };

        uploadModelHelper(model, subMeshes);
        uploadModelHelper(modelTransparent, subMeshesTransparent);

        isMeshUploaded = true;
    }

    void draw() {
        if (isMeshUploaded && model.meshCount > 0) {
            DrawModel(model, Vector3Zero(), 1.0f, WHITE);
        }
    }

    void drawTransparent() {
        if (isMeshUploaded && modelTransparent.meshCount > 0) {
            DrawModel(modelTransparent, Vector3Zero(), 1.0f, WHITE);
        }
    }
};
