# AntiGravity Procedural Voxel Engine

A premium, high-performance C++ voxel engine built on **Raylib** and **OpenGL 3.3 Core**. It implements advanced, highly-optimized voxel rendering algorithms, infinite terrain generation, and robust parallel chunk meshing.

---

## 🌟 Key Features

*   **⚡ Rock-Solid 120 FPS**: Locked performance target with seamless real-time rendering.
*   **🎮 Dedicated GPU Prioritization**: Automatic high-performance dedicated graphics (NVIDIA / AMD) card selection on Windows, with automatic integrated graphics fallback.
*   **🌍 Infinite Terrain Generation**: Fully procedural world generation using multidimensional fractional noise (sand beaches, stone cliffs, seabed generation, and foliage).
*   **🧵 Multi-Threaded Meshing Pipeline**: Background worker thread pool manages chunk generation and meshing without stuttering or blocking the main rendering loop.
*   **🔮 Two-Pass Transparency Pass**:
    *   *Pass 1*: Renders opaque solid voxel geometry with depth-writes enabled.
    *   *Pass 2*: Renders transparent water geometry with depth-writes disabled, enabling sandy and stone seabeds to blend realistically below the translucent ocean surface.
*   **🌓 Isotropic Per-Voxel Ambient Occlusion**:
    *   Exposed faces mesh into strict **1x1 quads** to keep ambient occlusion shadows sharp, localized, and undistorted.
    *   Dynamically splits quads along the optimal diagonal based on corner values to ensure perfectly symmetric, isotropic crevice lighting (avoiding structural anisotropy glitches).
*   **📈 Dynamic Projection Far-Culling**: Supports a view distance of up to **64 chunks**. Automatically scales the perspective far culling plane (up to $3,072$ blocks) to prevent background chunk clipping.
*   **🎛️ Glassmorphism HUD**: A sleek, translucent UI showing performance metrics (FPS, loaded chunks, background job counts, flight speeds) alongside interactive, throttled slider controls that prevent thread pool flooding.

---

## 🕹️ Controls Cheat Sheet

| Input | Action |
| :--- | :--- |
| **Hold [Right Click]** | Look around (Capture cursor) |
| **W / A / S / D** | Fly forward, backward, left, right |
| **SPACE** | Fly upward |
| **LEFT-SHIFT** | Fly downward |
| **MOUSE WHEEL** | Adjust flight movement speed |
| **Release [Right Click]** | Release cursor control to interact with UI |

---

## 🛠️ Build and Run Guide

### Prerequisites
Ensure you have the following installed on your system:
*   **C++ Compiler** with C++17 support (e.g., MSVC on Windows, GCC or Clang on Linux/macOS).
*   **CMake** (version 3.15 or newer).
*   **Raylib** dependencies (automatically fetched and configured via CMake FetchContent).

### 🚀 Running the Engine (Windows)
A simple, robust PowerShell build script is provided at the root directory to compile and launch the game instantly:

1. Open PowerShell inside the project root directory.
2. Run the build and launch script:
    ```powershell
    ./build.ps1
    ```
This script will automatically configure CMake, compile the project in **Release** mode under MSVC, and launch the engine executable.

### 🐧 Running the Engine (Linux / macOS)
To configure, compile, and run manually using CMake:

1. Initialize the build files:
    ```bash
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    ```
2. Build the project:
    ```bash
    cmake --build . --config Release
    ```
3. Run the executable:
    ```bash
    ./VoxelEngine
    ```

---

## 📂 Architecture Overview

*   [`src/main.cpp`](file:///c:/src/voxel-engine/src/main.cpp): Application entry point, rendering loops, GPU prioritization symbols, and the glassmorphism UI system.
*   [`src/voxel/World.hpp`](file:///c:/src/voxel-engine/src/voxel/World.hpp): Manages chunk positions around the player, garbage collection of distant chunks, and background thread task allocation.
*   [`src/voxel/Chunk.hpp`](file:///c:/src/voxel-engine/src/voxel/Chunk.hpp): The core mathematical heart of the voxel engine. Manages generation logic, coordinate region mapping, 8-neighbor boundary queries, and optimized 1x1 quad isotropic ambient occlusion meshing.
*   [`src/voxel/Voxel.hpp`](file:///c:/src/voxel-engine/src/voxel/Voxel.hpp): Contains definitions for different block types (air, grass, dirt, stone, sand, snow, wood, leaves, water).
*   [`src/camera/Camera.hpp`](file:///c:/src/voxel-engine/src/camera/Camera.hpp): High-precision flying camera controller.
*   [`src/core/`](file:///c:/src/voxel-engine/src/core): Includes helper modules for parallel processing (`ThreadPool.hpp`) and fractional noise generation (`Noise.hpp`).
