# Voxel Engine Build and Run Script for Windows (MSVC 2026 Build Tools)
$ErrorActionPreference = "Stop"

$VS_PATH = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
$VCVARS = "$VS_PATH\VC\Auxiliary\Build\vcvars64.bat"
$CMAKE = "$VS_PATH\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host "   Voxel Engine - Build and Run Script        " -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan

# Check if VS Build Tools exist
if (-not (Test-Path $VCVARS)) {
    Write-Error "Could not find Visual Studio 2026 Build Tools developer environment at: $VCVARS"
    exit 1
}

# Clean build directory if requested via -Clean parameter
if ($args -contains "-clean") {
    if (Test-Path "build") {
        Write-Host "Cleaning build directory..." -ForegroundColor Yellow
        Remove-Item -Path "build" -Recurse -Force
    }
}

# Generate project build files using CMake
Write-Host "Generating CMake build files..." -ForegroundColor Green
$cmdGen = "`"$VCVARS`" && `"$CMAKE`" -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
cmd.exe /c $cmdGen

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake Generation failed!" -ForegroundColor Red
    exit 1
}

# Compile in Release mode
Write-Host "Building project in Release mode..." -ForegroundColor Green
$cmdBuild = "`"$VCVARS`" && `"$CMAKE`" --build build --config Release"
cmd.exe /c $cmdBuild

if ($LASTEXITCODE -ne 0) {
    Write-Host "Compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Build successful!" -ForegroundColor Green
Write-Host "==============================================" -ForegroundColor Cyan

# Run the executable unless -norun is specified
if ($args -contains "-norun") {
    Write-Host "Compilation complete. Skipping launch due to -norun parameter." -ForegroundColor Yellow
} else {
    $EXE_PATH = "build\Release\VoxelEngine.exe"
    if (Test-Path $EXE_PATH) {
        Write-Host "Launching Voxel Engine..." -ForegroundColor Yellow
        & $EXE_PATH
    } else {
        Write-Host "Executable not found at: $EXE_PATH" -ForegroundColor Red
    }
}
