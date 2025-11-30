#include "vdb_compressor.h"
#include <map>
#include <cmath>
#include <limits>
#include <algorithm> // For std::max, std::min
#include <cmath>     // For std::abs


vdb_compressor::vdb_compressor(openvdb::FloatGrid::Ptr grid, float quality)
    : m_grid(grid), m_quality(quality), m_background_value(0.0f) 
{
    // Ensure grid is prepared
    if (!m_grid) return;

    compute_background_value();
}

vdb_compressor::~vdb_compressor() {
    // even empty is fine
}

void vdb_compressor::compute_background_value() {
    std::cout << "[VDB Compress] Using grid background value as B: " << m_grid->background() << std::endl;
    m_background_value = m_grid->background();
    
    // [Source: 163] We assume this value is the representative of empty space
}


void vdb_compressor::compute_brick_ranges() {
    m_bricks.clear();

    // [Source: 164] Form bricks of size 32x32x32 to cover the original volume.
    // We use 32 because it corresponds to 2^5, covering 64 standard VDB leaf nodes.
    const int BRICK_DIM = 32;

    // 1. Determine the extent of the active data
    openvdb::CoordBBox bbox = m_grid->evalActiveVoxelBoundingBox();
    
    // Align the start coordinates to the nearest 32-voxel boundary
    // to ensure our bricks align neatly with the VDB grid structure.
    openvdb::Coord min_c = bbox.min();
    min_c.setX(min_c.x() & ~(BRICK_DIM - 1));
    min_c.setY(min_c.y() & ~(BRICK_DIM - 1));
    min_c.setZ(min_c.z() & ~(BRICK_DIM - 1));
    
    openvdb::Coord max_c = bbox.max();

    // Use an Accessor for O(1) lookups inside the loop [Source: 30]
    auto accessor = m_grid->getAccessor();

    // 2. Iterate through the volume in steps of 32 (Brick by Brick)
    for (int z = min_c.z(); z <= max_c.z(); z += BRICK_DIM) {
        for (int y = min_c.y(); y <= max_c.y(); y += BRICK_DIM) {
            for (int x = min_c.x(); x <= max_c.x(); x += BRICK_DIM) {
                
                BrickInfo brick;
                brick.origin = openvdb::Coord(x, y, z);
                
                // Initialize range to inverse extremes
                brick.lo = std::numeric_limits<float>::max();
                brick.hi = std::numeric_limits<float>::lowest();
                
                bool has_active_content = false;

                // 3. Scan the 32x32x32 region to find min/max (lo/hi) [Source: 165]
                for (int bz = 0; bz < BRICK_DIM; ++bz) {
                    for (int by = 0; by < BRICK_DIM; ++by) {
                        for (int bx = 0; bx < BRICK_DIM; ++bx) {
                            // Calculate absolute voxel coordinate
                            openvdb::Coord c = brick.origin.offsetBy(bx, by, bz);
                            
                            // Check if this specific voxel has data
                            // [Source: 150] We sample at exact integer voxel positions
                            if (accessor.isValueOn(c)) {
                                float val = accessor.getValue(c);
                                
                                // Update Range
                                if (val < brick.lo) brick.lo = val;
                                if (val > brick.hi) brick.hi = val;
                                
                                has_active_content = true;
                            }
                        }
                    }
                }

                // Only store bricks that actually contain data.
                // Bricks that are completely empty are implicitly handled as background later.
                if (has_active_content) {
                    m_bricks.push_back(brick);
                }
            }
        }
    }
}



// [Source: 176] Equation (1): Closest Point-in-Range
// Measures the shortest distance from the interval [lo, hi] to B.
// If B is inside [lo, hi], the distance is 0.
float vdb_compressor::calculate_f1(const BrickInfo& brick) const {
    float dist_lo = std::abs(brick.lo - m_background_value);
    float dist_hi = std::abs(brick.hi - m_background_value);
    
    // If background is within the range, the closest distance is effectively zero
    if (m_background_value >= brick.lo && m_background_value <= brick.hi) {
        return 0.0f;
    }
    
    return std::min(dist_lo, dist_hi);
}

// [Source: 177] Equation (2): Farthest Point-in-Range
// Measures the maximum possible deviation from the background within this brick.
// Paper notes this metric (and f3) generally outperforms f1 [Source: 316, 322].
float vdb_compressor::calculate_f2(const BrickInfo& brick) const {
    float dist_lo = std::abs(brick.lo - m_background_value);
    float dist_hi = std::abs(brick.hi - m_background_value);
    return std::max(dist_lo, dist_hi);
}

// [Source: 178] Equation (3): Median Point-in-Range
// Measures the distance from the center of the brick's value range to B.
float vdb_compressor::calculate_f3(const BrickInfo& brick) const {
    float median = (brick.lo + brick.hi) / 2.0f;
    return std::abs(median - m_background_value);
}

// UPDATED COMPRESS FUNCTION
openvdb::FloatGrid::Ptr vdb_compressor::compress(const std::string& metric_name, bool use_roi, float roi_min, float roi_max) {

    // ---------------------------------------------------------
    // PRE-COMPRESSION CHECK
    // ---------------------------------------------------------
    uint64_t count_before = m_grid->activeVoxelCount();
    
    // ---------------------------------------------------------
    // PHASE 1 & 2: Analysis and Decomposition
    // ---------------------------------------------------------
    //[cite_start]// [cite: 157, 164] Compute B and slice volume into 32^3 bricks
    compute_background_value();
    compute_brick_ranges();

    // ---------------------------------------------------------
    // PHASE 3: Similarity Calculation & ROI BOOST
    // ---------------------------------------------------------
    //[cite_start]// [cite: 176-183] Calculate score for every brick based on user choice
    
    // The Boost Multiplier: High enough to override the distance-from-background metric
    const float ROI_BOOST_FACTOR = 255.0f; 

    for (auto& brick : m_bricks) {
        // 1. Calculate Base Similarity (The Paper's Logic)
        if (metric_name == "f1") {
            brick.similarity = calculate_f1(brick); 
        } else if (metric_name == "f2") {
            brick.similarity = calculate_f2(brick); 
        } else {
            brick.similarity = calculate_f3(brick); 
        }

        // 2. Apply ROI Extension (Your Contribution)
        if (use_roi) {
            // Check if this brick contains ANY values within the User's ROI.
            // Since compute_brick_ranges() already found the min (lo) and max (hi) 
            // for this specific brick, we can check for range overlap.
            
            // Logic: Overlap exists if (BrickLow <= ROIMax) AND (BrickHigh >= ROIMin)
            bool overlaps_roi = (brick.lo <= roi_max) && (brick.hi >= roi_min);

            if (overlaps_roi) {
                // Boost the score significantly. 
                // This forces the sort (Phase 4) to prioritize this brick 
                // even if it is numerically close to the background.
                brick.similarity *= ROI_BOOST_FACTOR;
            }
        }
    }

    // ---------------------------------------------------------
    // PHASE 4: Sorting (The Priority Queue)
    // ---------------------------------------------------------
    // [cite_start]// [cite: 166, 167] Sort bricks based on similarity (descending).
    // // Because of Phase 3, ROI bricks now have huge scores and float to the top.
    std::sort(m_bricks.begin(), m_bricks.end(), 
        [](const BrickInfo& a, const BrickInfo& b) {
            return a.similarity > b.similarity; 
    });

    // ---------------------------------------------------------
    // PHASE 5: Determine Fixed-Rate Cutoff
    // ---------------------------------------------------------
    size_t num_bricks_total = m_bricks.size();
    size_t bricks_to_keep = static_cast<size_t>(num_bricks_total * m_quality);
    if (bricks_to_keep > num_bricks_total) bricks_to_keep = num_bricks_total;

    // ---------------------------------------------------------
    // PHASE 6: Synthesis (Reconstruction)
    // ---------------------------------------------------------
    openvdb::FloatGrid::Ptr compressed_grid = openvdb::FloatGrid::create(m_background_value);
    compressed_grid->setTransform(m_grid->transform().copy());
    
    auto target_acc = compressed_grid->getAccessor();
    auto source_acc = m_grid->getAccessor();
    const int BRICK_DIM = 32;

    for (size_t i = 0; i < bricks_to_keep; ++i) {
        const auto& brick = m_bricks[i];
        
        for (int z = 0; z < BRICK_DIM; ++z) {
            for (int y = 0; y < BRICK_DIM; ++y) {
                for (int x = 0; x < BRICK_DIM; ++x) {
                    openvdb::Coord c = brick.origin.offsetBy(x, y, z);
                    
                    if (source_acc.isValueOn(c)) {
                        target_acc.setValue(c, source_acc.getValue(c));
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------
    // PHASE 7: Optimization & Stats
    // ---------------------------------------------------------
    compressed_grid->tree().prune();
    
    // Optional: Debug output to confirm ROI usage
    if (use_roi) {
        std::cout << "[VDB Compress] Applied ROI Boost [" << roi_min << ", " << roi_max << "]" << std::endl;
    }

    uint64_t count_after = compressed_grid->activeVoxelCount();
    std::cout << "[VDB Compress] Final Voxel Count: " << count_after << std::endl;

    return compressed_grid;
}