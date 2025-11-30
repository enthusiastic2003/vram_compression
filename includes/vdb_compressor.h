#pragma once
#include <openvdb/openvdb.h>
#include <vector>
#include <string>

// Helper struct (ensure this is visible in your header)
struct BrickInfo {
    openvdb::Coord origin;
    float lo, hi;       // The range of values found in this brick
    float similarity;   // The calculated importance score
};

class vdb_compressor {
public:
    vdb_compressor(openvdb::FloatGrid::Ptr grid, float quality);
    ~vdb_compressor();

    // UPDATED SIGNATURE: Added use_roi, roi_min, roi_max
    openvdb::FloatGrid::Ptr compress(const std::string& metric_name, bool use_roi, float roi_min, float roi_max);

private:
    void compute_background_value();
    void compute_brick_ranges();
    
    float calculate_f1(const BrickInfo& brick) const;
    float calculate_f2(const BrickInfo& brick) const;
    float calculate_f3(const BrickInfo& brick) const;

    openvdb::FloatGrid::Ptr m_grid;
    float m_quality;
    float m_background_value;
    std::vector<BrickInfo> m_bricks;
};