# VRAM Compression Viewer

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue) ![CUDA](https://img.shields.io/badge/CUDA-NanoVDB-green) ![OpenGL](https://img.shields.io/badge/OpenGL-Rendering-orange) ![ImGui](https://img.shields.io/badge/UI-ImGui-lightgrey) ![Status](https://img.shields.io/badge/State-Work_in_progress-yellow)

An interactive GPU-accelerated volume renderer for experimenting with VRAM compression of volumetric datasets. It loads VTK volumes, compresses them to NanoVDB, and renders them via CUDA/OpenGL interop with an ImGui-powered control panel for evaluating quality and tweaking transfer functions.

## Table of Contents
- [Features](#features)
- [Quick Start](#quick-start)
- [Usage](#usage)
- [Controls \& UI](#controls--ui)
- [Project Layout](#project-layout)
- [Development](#development)
- [Troubleshooting](#troubleshooting)

## Features
- **VTK volume ingestion** with a VoxelLoader pipeline.
- **CUDA/OpenGL interop** for real-time rendering of compressed volumes.
- **NanoVDB compression** helpers plus quality metrics (MSE/PSNR) to evaluate fidelity.
- **Transfer function editor** with color/alpha controls and histogram preview.
- **Region of interest toggles** to focus on specific intensity ranges.
- **ImGui overlay** for on-screen stats, FPS, and dataset metadata.

## Quick Start
Prerequisites:
- C++17 toolchain, `g++`/`gcc`
- CUDA toolkit with `nvcc` available on PATH
- OpenGL drivers, GLFW, GLAD, ImGui, OpenVDB/NanoVDB libraries

Clone and build:
```bash
make BUILD=release   # or BUILD=debug
```

Run with a VTK volume:
```bash
./bin/release/viz3d <path_to_volume.vtk>
```
The build artifacts live under `bin/<build>/` and intermediates under `build/`.

## Usage
1. Launch the app with a `.vtk` file.
2. The window opens at 1280x900 with the dataset name derived from the file.
3. Use the UI to adjust compression quality, transfer functions, and ROIs; watch FPS and quality metrics update live.

## Controls & UI
- **Camera**: Mouse drag to orbit, scroll to zoom (handled via `Camera` controls).
- **Transfer Function**: Two-color gradient with alpha thresholds; histogram preview to guide ranges.
- **Compression Panel**: Toggle compression quality, recompute, and inspect MSE/PSNR metrics.
- **ROI Toggles**: Enable/disable region-of-interest filtering with configurable min/max.
- **Screenshots/Exports**: Renderer tags captures with the dataset name for easier tracing.

## Project Layout
```
Makefile                # Build targets for release/debug with CUDA/OpenGL/NanoVDB
src/                    # Application sources (entry point in main.cpp)
includes/               # Core headers (renderer, loader, metrics, camera, shaders)
packages/               # Third-party dependencies (glad, imgui, glm, etc.)
shaders/                # GLSL shader programs copied into bin/<build>/shaders/
visual_examination_tools# Auxiliary inspection utilities
```

## Development
- **Build modes**: `make BUILD=release` (default) or `make BUILD=debug` for symbols.
- **Clean**: `make clean`
- **Run after build**: `make run` (invokes the release build by default).
- Ensure CUDA architecture flags in the `Makefile` match your GPU for optimal performance.

## Troubleshooting
- Missing libraries? Verify `pkg-config` can locate `glfw3` and that OpenVDB/NanoVDB dev packages are installed.
- CUDA link errors: confirm `nvcc` matches your installed driver and that `-lcudart` is available.
- Rendering issues: update GPU drivers and check that your OpenGL context supports required extensions.

> Roadmap: support for additional VTK data types (e.g., double) and expanded dataset loaders.
