#include "metrics.h"
#include <iostream>
#include <cmath>
#include <limits>

float QualityMetrics::sample_grid(const openvdb::FloatGrid::Ptr& grid, const openvdb::Coord& coord) {
    if (!grid) return 0.0f;
    auto accessor = grid->getAccessor();
    return accessor.getValue(coord);
}

QualityMetrics::Metrics QualityMetrics::calculate_quality_metrics(
    openvdb::FloatGrid::Ptr original_grid, 
    openvdb::FloatGrid::Ptr compressed_grid) 
{
    Metrics metrics;
    
    if (!original_grid || !compressed_grid) {
        std::cerr << "Error: One or both grids are null" << std::endl;
        return metrics;
    }
    
    auto original_accessor = original_grid->getAccessor();
    auto compressed_accessor = compressed_grid->getAccessor();
    
    // Get the bounding box of the original grid
    openvdb::CoordBBox bbox = original_grid->evalActiveVoxelBoundingBox();
    
    float max_value = std::numeric_limits<float>::lowest();
    float sum_squared_errors = 0.0f;
    size_t voxel_count = 0;
    
    std::cout << "Calculating quality metrics..." << std::endl;
    std::cout << "Bounding box: min(" << bbox.min().x() << ", " << bbox.min().y() << ", " << bbox.min().z() 
              << ") max(" << bbox.max().x() << ", " << bbox.max().y() << ", " << bbox.max().z() << ")" << std::endl;
    
    // Iterate through all voxels in the original bounding box
    for (int z = bbox.min().z(); z <= bbox.max().z(); ++z) {
        for (int y = bbox.min().y(); y <= bbox.max().y(); ++y) {
            for (int x = bbox.min().x(); x <= bbox.max().x(); ++x) {
                openvdb::Coord coord(x, y, z);
                
                // Get original value
                float original_val = original_accessor.getValue(coord);
                
                // Get compressed value (will return background if not active)
                float compressed_val = compressed_accessor.getValue(coord);
                
                // Update max value for PSNR calculation
                if (original_val > max_value) {
                    max_value = original_val;
                }
                
                // Calculate squared error
                float error = original_val - compressed_val;
                sum_squared_errors += error * error;
                voxel_count++;
            }
        }
    }
    
    // Calculate MSE and PSNR
    if (voxel_count > 0) {
        metrics.mse = sum_squared_errors / voxel_count;
        if (metrics.mse > 0.0f) {
            metrics.psnr = 20.0f * log10(max_value) - 10.0f * log10(metrics.mse);
        } else {
            metrics.psnr = std::numeric_limits<float>::infinity(); // Perfect reconstruction
        }
        metrics.compared_voxels = voxel_count;
        metrics.metrics_calculated = true;
        
        std::cout << "Quality metrics calculated:" << std::endl;
        std::cout << "  Compared voxels: " << voxel_count << std::endl;
        std::cout << "  Max value: " << max_value << std::endl;
        std::cout << "  MSE: " << metrics.mse << std::endl;
        std::cout << "  PSNR: " << metrics.psnr << " dB" << std::endl;
    } else {
        std::cerr << "Error: No voxels compared" << std::endl;
    }
    
    return metrics;
}
