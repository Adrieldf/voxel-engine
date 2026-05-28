#pragma once

#include "raylib.h"
#include <cstdint>

enum BlockType : uint8_t {
    BLOCK_AIR = 0,
    BLOCK_GRASS = 1,
    BLOCK_DIRT = 2,
    BLOCK_STONE = 3,
    BLOCK_SAND = 4,
    BLOCK_SNOW = 5,
    BLOCK_WOOD = 6,
    BLOCK_LEAVES = 7,
    BLOCK_WATER = 8,
    BLOCK_COUNT
};

struct BlockInfo {
    bool isSolid;
    bool isTransparent;
    Color color;
    const char* name;
};

const BlockInfo BLOCK_TABLE[BLOCK_COUNT] = {
    // Air
    { false, true, Color{ 0, 0, 0, 0 }, "Air" },
    // Grass
    { true, false, Color{ 46, 184, 114, 255 }, "Grass" },
    // Dirt
    { true, false, Color{ 115, 76, 56, 255 }, "Dirt" },
    // Stone
    { true, false, Color{ 108, 122, 137, 255 }, "Stone" },
    // Sand
    { true, false, Color{ 230, 204, 145, 255 }, "Sand" },
    // Snow
    { true, false, Color{ 242, 245, 248, 255 }, "Snow" },
    // Wood
    { true, false, Color{ 90, 62, 45, 255 }, "Wood" },
    // Leaves
    { true, false, Color{ 39, 140, 85, 255 }, "Leaves" },
    // Water
    { false, true, Color{ 41, 128, 185, 180 }, "Water" }
};

inline BlockInfo GetBlockInfo(BlockType type) {
    if (type >= BLOCK_COUNT) return { false, true, Color{ 255, 0, 255, 255 }, "Unknown" };
    return BLOCK_TABLE[type];
}
