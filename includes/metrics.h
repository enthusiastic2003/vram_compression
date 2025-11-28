#ifndef METRICS_H
#define METRICS_H

#include <openvdb/openvdb.h>

class QualityMetrics {
public:
    struct Metrics {
        float mse;
        float psnr;
        size_t compared_voxels;
        bool metrics_calculated;
        
        Metrics() : mse(0.0f), psnr(0.0f), compared_voxels(0), metrics_calculated(false) {}
    };

    // Calculate MSE and PSNR between original and compressed grids
    static Metrics calculate_quality_metrics(openvdb::FloatGrid::Ptr original_grid, 
                                           openvdb::FloatGrid::Ptr compressed_grid);

private:
    // Helper method to sample grid at coordinate
    static float sample_grid(const openvdb::FloatGrid::Ptr& grid, const openvdb::Coord& coord);
};

#endif // METRICS_H
