#include <openvdb/openvdb.h>
#include <vector>
#include <algorithm>

//  Bricks are defined as 32x32x32 regions
struct BrickInfo {
    openvdb::Coord origin; // Spatial location of the brick
    float lo;              // [cite: 165] Min value in this brick
    float hi;              // [cite: 165] Max value in this brick
    float similarity;      // Score calculated by f1, f2, or f3
};

class vdb_compressor {
public:
    // [cite: 149] Inputs: volume, W, H, D (implicit in grid), and quality
    vdb_compressor(openvdb::FloatGrid::Ptr grid, float quality);
    ~vdb_compressor();

    // Main execution function
    openvdb::FloatGrid::Ptr compress(const std::string& metric_name);

private:
    openvdb::FloatGrid::Ptr m_grid;
    float m_quality;           // User provided quality parameter [0:1] [cite: 148]
    float m_background_value;  // [cite: 160, 162] The value B (ArgMax of histogram)
    
    // [cite: 156] Grid is decomposed into bricks (32^3)
    std::vector<BrickInfo> m_bricks; 

    // Step 1: Compute histogram and find Background Value B 
    void compute_background_value();

    // Step 2: Divide grid into bricks and compute [lo, hi] for each [cite: 164, 165]
    void compute_brick_ranges();

    // Step 3: Similarity Metrics [cite: 176-178]
    // These calculate the distance of the brick range to m_background_value
    float calculate_f1(const BrickInfo& brick) const; // Closest point [cite: 176]
    float calculate_f2(const BrickInfo& brick) const; // Farthest point [cite: 177]
    float calculate_f3(const BrickInfo& brick) const; // Median point [cite: 178]

    // Helper to perform the sorting 
    void sort_bricks(const std::string& metric_name);
};