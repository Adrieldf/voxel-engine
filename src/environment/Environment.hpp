
#pragma once

#include "core/Noise.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <cmath>

// Draw a beautiful skybox with a vertical sky color gradient (Deep zenith to
// pastel horizon)
inline void DrawSkybox(Vector3 cameraPos) {
  float R = 400.0f; // Perfect size: diagonal (approx 600) is well within 1000
                    // far plane clip limit to prevent clipping, but centers
                    // exactly on camera so it wraps infinitely

  float x0 = cameraPos.x - R;
  float x1 = cameraPos.x + R;
  float y0 = cameraPos.y - R * 0.5f;
  float y1 = cameraPos.y + R;
  float z0 = cameraPos.z - R;
  float z1 = cameraPos.z + R;

  Color zenith = Color{32, 98, 192, 255};    // Rich deep sky blue
  Color horizon = Color{197, 223, 242, 255}; // Light pastel sky blue

  rlDisableDepthMask();
  rlDisableBackfaceCulling();

  rlDisableTexture(); // Disable texturing to prevent glitched stretch of the
                      // last bound texture over the skybox
  rlBegin(RL_QUADS);
  // 1. Front face (z = z0)
  rlColor4ub(horizon.r, horizon.g, horizon.b, horizon.a);
  rlVertex3f(x0, y0, z0);
  rlVertex3f(x1, y0, z0);
  rlColor4ub(zenith.r, zenith.g, zenith.b, zenith.a);
  rlVertex3f(x1, y1, z0);
  rlVertex3f(x0, y1, z0);

  // 2. Back face (z = z1)
  rlColor4ub(horizon.r, horizon.g, horizon.b, horizon.a);
  rlVertex3f(x1, y0, z1);
  rlVertex3f(x0, y0, z1);
  rlColor4ub(zenith.r, zenith.g, zenith.b, zenith.a);
  rlVertex3f(x0, y1, z1);
  rlVertex3f(x1, y1, z1);

  // 3. Left face (x = x0)
  rlColor4ub(horizon.r, horizon.g, horizon.b, horizon.a);
  rlVertex3f(x0, y0, z1);
  rlVertex3f(x0, y0, z0);
  rlColor4ub(zenith.r, zenith.g, zenith.b, zenith.a);
  rlVertex3f(x0, y1, z0);
  rlVertex3f(x0, y1, z1);

  // 4. Right face (x = x1)
  rlColor4ub(horizon.r, horizon.g, horizon.b, horizon.a);
  rlVertex3f(x1, y0, z0);
  rlVertex3f(x1, y0, z1);
  rlColor4ub(zenith.r, zenith.g, zenith.b, zenith.a);
  rlVertex3f(x1, y1, z1);
  rlVertex3f(x1, y1, z0);

  // 5. Top face (y = y1)
  rlColor4ub(zenith.r, zenith.g, zenith.b, zenith.a);
  rlVertex3f(x0, y1, z0);
  rlVertex3f(x1, y1, z0);
  rlVertex3f(x1, y1, z1);
  rlVertex3f(x0, y1, z1);

  // 6. Bottom face (y = y0)
  rlColor4ub(horizon.r, horizon.g, horizon.b, horizon.a);
  rlVertex3f(x0, y0, z1);
  rlVertex3f(x1, y0, z1);
  rlVertex3f(x1, y0, z0);
  rlVertex3f(x0, y0, z0);
  rlEnd();

  rlEnableBackfaceCulling();
  rlEnableDepthMask();
}

// Draw thick, Minecraft-like drifting clouds with subtle wireframe outlines
inline void DrawClouds(Vector3 playerPos, Vector3 cameraForward,
                       const Noise &noise, float dt) {
  static double cloudTime = 0.0;
  cloudTime += dt;

  float cellSize = 48.0f;
  float cloudHeight = 220.0f;
  float cloudThickness = 8.0f;
  int radius =
      16; // 16 cells in each direction is performant and looks infinite

  int px = static_cast<int>(std::floor(playerPos.x / cellSize));
  int pz = static_cast<int>(std::floor(playerPos.z / cellSize));

  Color cloudColor =
      Color{255, 255, 255, 180}; // Beautiful semi-transparent cloud

  auto isCloud = [&](int x, int z) -> bool {
    double nx = x * 0.08 + cloudTime * 0.003;
    double nz = z * 0.08 + cloudTime * 0.001;
    return noise.noise(nx, nz) > 0.25;
  };

  rlDisableTexture();
  rlDisableDepthMask();

  // Pass 1: Render solid, cohesive transparent cloud quads in a single
  // high-performance batch. We completely omit all internal shared faces
  // between adjacent cloud blocks, which fully solves the overlapping
  // semi-transparency issue!
  rlBegin(RL_QUADS);
  rlColor4ub(cloudColor.r, cloudColor.g, cloudColor.b, cloudColor.a);

  for (int x = -radius; x <= radius; ++x) {
    for (int z = -radius; z <= radius; ++z) {
      int cx = px + x;
      int cz = pz + z;

      if (isCloud(cx, cz)) { // Check for cloud presence
        Vector3 position = {cx * cellSize + cellSize * 0.5f, cloudHeight,
                            cz * cellSize + cellSize * 0.5f};

        // Camera frustum culling for clouds to ensure fast framerates
        Vector3 toCloud = Vector3Subtract(position, playerPos);
        float dist = Vector3Length(toCloud);
        if (dist > 100.0f) {
          Vector3 dir = Vector3Scale(toCloud, 1.0f / dist);
          float dot = Vector3DotProduct(cameraForward, dir);
          if (dot < 0.2f)
            continue; // Culled!
        }

        // Calculate 3D bounds for drawing custom quads
        float x0 = position.x - cellSize * 0.5f;
        float x1 = position.x + cellSize * 0.5f;
        float y0 = cloudHeight - cloudThickness * 0.5f;
        float y1 = cloudHeight + cloudThickness * 0.5f;
        float z0 = position.z - cellSize * 0.5f;
        float z1 = position.z + cellSize * 0.5f;

        bool hasLeft = isCloud(cx - 1, cz);
        bool hasRight = isCloud(cx + 1, cz);
        bool hasFront = isCloud(cx, cz - 1);
        bool hasBack = isCloud(cx, cz + 1);

        // Top face (always visible)
        rlVertex3f(x0, y1, z0);
        rlVertex3f(x0, y1, z1);
        rlVertex3f(x1, y1, z1);
        rlVertex3f(x1, y1, z0);

        // Bottom face (always visible)
        rlVertex3f(x0, y0, z0);
        rlVertex3f(x1, y0, z0);
        rlVertex3f(x1, y0, z1);
        rlVertex3f(x0, y0, z1);

        // Left face boundary
        if (!hasLeft) {
          rlVertex3f(x0, y0, z0);
          rlVertex3f(x0, y0, z1);
          rlVertex3f(x0, y1, z1);
          rlVertex3f(x0, y1, z0);
        }
        // Right face boundary
        if (!hasRight) {
          rlVertex3f(x1, y0, z1);
          rlVertex3f(x1, y0, z0);
          rlVertex3f(x1, y1, z0);
          rlVertex3f(x1, y1, z1);
        }
        // Front face boundary
        if (!hasFront) {
          rlVertex3f(x1, y0, z0);
          rlVertex3f(x0, y0, z0);
          rlVertex3f(x0, y1, z0);
          rlVertex3f(x1, y1, z0);
        }
        // Back face boundary
        if (!hasBack) {
          rlVertex3f(x0, y0, z1);
          rlVertex3f(x1, y0, z1);
          rlVertex3f(x1, y1, z1);
          rlVertex3f(x0, y1, z1);
        }
      }
    }
  }
  rlEnd();

  // Pass 2: Render crisp borders on outer boundary edges of cloud clusters
  rlBegin(RL_LINES);
  Color wireColor = Color{255, 255, 255, 60};
  rlColor4ub(wireColor.r, wireColor.g, wireColor.b, wireColor.a);

  for (int x = -radius; x <= radius; ++x) {
    for (int z = -radius; z <= radius; ++z) {
      int cx = px + x;
      int cz = pz + z;

      if (isCloud(cx, cz)) { // Check for cloud presence
        Vector3 position = {cx * cellSize + cellSize * 0.5f, cloudHeight,
                            cz * cellSize + cellSize * 0.5f};

        // Camera frustum culling for clouds to ensure fast framerates
        Vector3 toCloud = Vector3Subtract(position, playerPos);
        float dist = Vector3Length(toCloud);
        if (dist > 100.0f) {
          Vector3 dir = Vector3Scale(toCloud, 1.0f / dist);
          float dot = Vector3DotProduct(cameraForward, dir);
          if (dot < 0.2f)
            continue; // Culled!
        }

        // Calculate 3D bounds for drawing custom wireframe outline
        float x0 = position.x - cellSize * 0.5f;
        float x1 = position.x + cellSize * 0.5f;
        float y0 = cloudHeight - cloudThickness * 0.5f;
        float y1 = cloudHeight + cloudThickness * 0.5f;
        float z0 = position.z - cellSize * 0.5f;
        float z1 = position.z + cellSize * 0.5f;

        bool hasLeft = isCloud(cx - 1, cz);
        bool hasRight = isCloud(cx + 1, cz);
        bool hasFront = isCloud(cx, cz - 1);
        bool hasBack = isCloud(cx, cz + 1);

        // Left face boundary
        if (!hasLeft) {
          rlVertex3f(x0, y0, z0);
          rlVertex3f(x0, y1, z0);
          rlVertex3f(x0, y0, z1);
          rlVertex3f(x0, y1, z1);
          rlVertex3f(x0, y0, z0);
          rlVertex3f(x0, y0, z1);
          rlVertex3f(x0, y1, z0);
          rlVertex3f(x0, y1, z1);
        }
        // Right face boundary
        if (!hasRight) {
          rlVertex3f(x1, y0, z0);
          rlVertex3f(x1, y1, z0);
          rlVertex3f(x1, y0, z1);
          rlVertex3f(x1, y1, z1);
          rlVertex3f(x1, y0, z0);
          rlVertex3f(x1, y0, z1);
          rlVertex3f(x1, y1, z0);
          rlVertex3f(x1, y1, z1);
        }
        // Front face boundary
        if (!hasFront) {
          rlVertex3f(x0, y0, z0);
          rlVertex3f(x0, y1, z0);
          rlVertex3f(x1, y0, z0);
          rlVertex3f(x1, y1, z0);
          rlVertex3f(x0, y0, z0);
          rlVertex3f(x1, y0, z0);
          rlVertex3f(x0, y1, z0);
          rlVertex3f(x1, y1, z0);
        }
        // Back face boundary
        if (!hasBack) {
          rlVertex3f(x0, y0, z1);
          rlVertex3f(x0, y1, z1);
          rlVertex3f(x1, y0, z1);
          rlVertex3f(x1, y1, z1);
          rlVertex3f(x0, y0, z1);
          rlVertex3f(x1, y0, z1);
          rlVertex3f(x0, y1, z1);
          rlVertex3f(x1, y1, z1);
        }
      }
    }
  }
  rlEnd();

  rlEnableDepthMask();
}
