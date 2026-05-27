cmake_minimum_required(VERSION 3.15)
project(VoxelEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force Release build optimizations by default if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

# Fetch raylib from GitHub
include(FetchContent)
FetchContent_Declare(
  raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
)

# Disable building raylib examples, tests, and games to speed up compile time
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RSHADERS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(raylib)

# Source files
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.hpp")

# Add executable
add_executable(VoxelEngine ${SOURCES})

# Target directories and libraries
target_include_directories(VoxelEngine PRIVATE src)
target_link_libraries(VoxelEngine PRIVATE raylib)

# Compiler specific flags (MSVC optimization settings)
if(MSVC)
  target_compile_options(VoxelEngine PRIVATE /W4 /WX- /O2 /Oi /Ot /Oy /GL /D_CRT_SECURE_NO_WARNINGS)
  target_link_options(VoxelEngine PRIVATE /INCREMENTAL:NO /OPT:REF /OPT:ICF /LTCG)
endif()
