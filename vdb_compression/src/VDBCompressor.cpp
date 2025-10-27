#include "VDBCompressor.h"
#include <openvdb/openvdb.h>
#include <vtkSmartPointer.h>
#include <vtkStructuredPointsReader.h>
#include <vtkImageData.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkStructuredPoints.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

VDBCompressor::VDBCompressor() {
    openvdb::initialize();
}

openvdb::FloatGrid::Ptr VDBCompressor::compressVTKVolume(
    const std::string& vtkFilename, 
    float quality, 
    int brickSize,
    int metricType) {
    
    // Load VTK data
    auto reader = vtkSmartPointer<vtkStructuredPointsReader>::New();
    reader->SetFileName(vtkFilename.c_str());
    reader->Update();
    
    // Use auto to avoid type casting issues
    auto vtkData = reader->GetOutput();
    
    if (!vtkData) {
        throw std::runtime_error("Failed to load VTK file: " + vtkFilename);
    }
    
    // Get volume dimensions and data
    int dims[3];
    vtkData->GetDimensions(dims);
    int W = dims[0], H = dims[1], D = dims[2];
    
    vtkPointData* pointData = vtkData->GetPointData();
    if (!pointData) {
        throw std::runtime_error("No point data in VTK file");
    }
    
    vtkDataArray* scalarData = pointData->GetScalars();
    if (!scalarData) {
        throw std::runtime_error("No scalar data in VTK file");
    }
    
    int totalVoxels = W * H * D;
    
    std::vector<float> volumeData(totalVoxels);
    for (int i = 0; i < totalVoxels; ++i) {
        volumeData[i] = static_cast<float>(scalarData->GetComponent(i, 0));
    }
    
    // Create empty OpenVDB grid
    openvdb::FloatGrid::Ptr grid = openvdb::FloatGrid::create();
    grid->setGridClass(openvdb::GRID_FOG_VOLUME);
    grid->setName("compressed_volume");
    
    // Compute background value using histogram
    float background = computeBackgroundValue(volumeData);
    
    // FIX: Use insertMeta("background", ...) instead of setBackground()
    // The background is automatically set when we create the grid
    // We'll handle background during the compression algorithm
    
    // Apply fixed-rate compression algorithm
    applyCompressionAlgorithm(grid, volumeData, W, H, D, background, quality, brickSize, metricType);
    
    return grid;
}

float VDBCompressor::computeBackgroundValue(const std::vector<float>& data) {
    std::vector<float> sortedData = data;
    std::sort(sortedData.begin(), sortedData.end());
    return sortedData[sortedData.size() / 2];
}

void VDBCompressor::applyCompressionAlgorithm(
    openvdb::FloatGrid::Ptr grid,
    const std::vector<float>& volumeData,
    int W, int H, int D,
    float background,
    float quality,
    int brickSize,
    int metricType) {
    
    auto& tree = grid->tree();
    
    // Phase 1: Create brick decomposition
    std::vector<Brick> bricks;
    decomposeIntoBricks(bricks, volumeData, W, H, D, brickSize, background, metricType);
    
    // Phase 2: Sort bricks by similarity to background
    std::sort(bricks.begin(), bricks.end());
    
    // Phase 3: Activate bricks based on quality parameter
    int totalBricks = bricks.size();
    int bricksToActivate = static_cast<int>(totalBricks * quality);
    
    std::cout << "Activating " << bricksToActivate << " out of " << totalBricks << " bricks" << std::endl;
    std::cout << "Background value: " << background << std::endl;
    
    // Always activate extreme corners first
    activateExtremeCorners(tree, volumeData, W, H, D);
    
    // Activate selected bricks
    for (int i = 0; i < bricksToActivate && i < totalBricks; ++i) {
        activateBrick(tree, bricks[i], volumeData, W, H, D, brickSize);
    }
    
    // Optimize memory
    tree.prune();
    std::cout << "Grid memory: " << grid->memUsage() << " bytes" << std::endl;
}

void VDBCompressor::decomposeIntoBricks(
    std::vector<Brick>& bricks,
    const std::vector<float>& volumeData,
    int W, int H, int D,
    int brickSize,
    float background,
    int metricType) {
    
    int bricksX = (W + brickSize - 1) / brickSize;
    int bricksY = (H + brickSize - 1) / brickSize;
    int bricksZ = (D + brickSize - 1) / brickSize;
    
    for (int bz = 0; bz < bricksZ; ++bz) {
        for (int by = 0; by < bricksY; ++by) {
            for (int bx = 0; bx < bricksX; ++bx) {
                Brick brick;
                brick.x = bx * brickSize;
                brick.y = by * brickSize;
                brick.z = bz * brickSize;
                
                computeBrickRange(brick, volumeData, W, H, D, brickSize);
                brick.similarity = computeSimilarity(brick.minVal, brick.maxVal, background, metricType);
                
                bricks.push_back(brick);
            }
        }
    }
}

void VDBCompressor::computeBrickRange(
    Brick& brick,
    const std::vector<float>& volumeData,
    int W, int H, int D,
    int brickSize) {
    
    brick.minVal = std::numeric_limits<float>::max();
    brick.maxVal = std::numeric_limits<float>::lowest();
    
    for (int z = brick.z; z < std::min(brick.z + brickSize, D); ++z) {
        for (int y = brick.y; y < std::min(brick.y + brickSize, H); ++y) {
            for (int x = brick.x; x < std::min(brick.x + brickSize, W); ++x) {
                int idx = z * W * H + y * W + x;
                float val = volumeData[idx];
                
                brick.minVal = std::min(brick.minVal, val);
                brick.maxVal = std::max(brick.maxVal, val);
            }
        }
    }
}

float VDBCompressor::computeSimilarity(float lo, float hi, float background, int metricType) {
    switch (metricType) {
        case 1:
            return std::min(std::abs(lo - background), std::abs(hi - background));
        case 2:
            return std::max(std::abs(lo - background), std::abs(hi - background));
        case 3:
        default:
            return std::abs((lo + hi) / 2.0f - background);
    }
}

void VDBCompressor::activateExtremeCorners(
    openvdb::FloatTree& tree,
    const std::vector<float>& volumeData,
    int W, int H, int D) {
    
    int corners[8][3] = {
        {0, 0, 0}, {W-1, 0, 0}, {0, H-1, 0}, {W-1, H-1, 0},
        {0, 0, D-1}, {W-1, 0, D-1}, {0, H-1, D-1}, {W-1, H-1, D-1}
    };
    
    for (int i = 0; i < 8; ++i) {
        int x = corners[i][0], y = corners[i][1], z = corners[i][2];
        int idx = z * W * H + y * W + x;
        float val = volumeData[idx];
        
        openvdb::Coord coord(x, y, z);
        tree.setValue(coord, val);
    }
}

void VDBCompressor::activateBrick(
    openvdb::FloatTree& tree,
    const Brick& brick,
    const std::vector<float>& volumeData,
    int W, int H, int D,
    int brickSize) {
    
    for (int z = brick.z; z < std::min(brick.z + brickSize, D); ++z) {
        for (int y = brick.y; y < std::min(brick.y + brickSize, H); ++y) {
            for (int x = brick.x; x < std::min(brick.x + brickSize, W); ++x) {
                int idx = z * W * H + y * W + x;
                float val = volumeData[idx];
                
                openvdb::Coord coord(x, y, z);
                tree.setValue(coord, val);
            }
        }
    }
}
