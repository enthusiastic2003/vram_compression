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
    // [Source: 157-162] Compute histogram and find B = ARGMAX(hist)
    
    // Using a map to bin values (simple histogram implementation)
    // In production, you might want more sophisticated binning for floats
    std::map<float, size_t> histogram;
    
    // Iterate over all active voxels to build histogram
    for (auto iter = m_grid->beginValueOn(); iter; ++iter) {
        float val = *iter;
        // Rounding to 2 decimal places to group similar floating point values
        float key = std::round(val * 100.0f) / 100.0f; 
        histogram[key]++;
    }

    // Find the value with the highest frequency (ARGMAX)
    size_t max_count = 0;
    for (const auto& pair : histogram) {
        if (pair.second > max_count) {
            max_count = pair.second;
            m_background_value = pair.first;
        }
    }
    
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

openvdb::FloatGrid::Ptr vdb_compressor::compress(const std::string& metric_name) {

    // ---------------------------------------------------------
    // PRE-COMPRESSION CHECK
    // ---------------------------------------------------------
    // Count active voxels in the original full volume
    uint64_t count_before = m_grid->activeVoxelCount();
    std::cout << "[VDB Compress] Original Active Voxels: " << count_before << std::endl;

    // ---------------------------------------------------------
    // PHASE 1 & 2: Analysis and Decomposition
    // ---------------------------------------------------------
    // [cite: 157, 164] Compute B and slice volume into 32^3 bricks
    compute_background_value();
    compute_brick_ranges();

    // ---------------------------------------------------------
    // PHASE 3: Similarity Calculation
    // ---------------------------------------------------------
    // [cite: 176-183] Calculate score for every brick based on user choice
    for (auto& brick : m_bricks) {
        if (metric_name == "f1") {
            brick.similarity = calculate_f1(brick); // Closest point
        } else if (metric_name == "f2") {
            brick.similarity = calculate_f2(brick); // Farthest point
        } else {
            brick.similarity = calculate_f3(brick); // Median (Default/Superior)
        }
    }

    // ---------------------------------------------------------
    // PHASE 4: Sorting (The Priority Queue)
    // ---------------------------------------------------------
    // [cite: 166, 167] Sort bricks based on similarity to background.
    // Logic: Higher similarity score = Larger distance from background.
    // We sort DESCENDING so the most "important" bricks are at index 0.
    std::sort(m_bricks.begin(), m_bricks.end(), 
        [](const BrickInfo& a, const BrickInfo& b) {
            return a.similarity > b.similarity; 
    });

    // ---------------------------------------------------------
    // PHASE 5: Determine Fixed-Rate Cutoff
    // ---------------------------------------------------------
    //  Map user quality (0.0 to 1.0) to exact number of bricks.
    // This guarantees the output size fits the "fixed rate" constraint.
    size_t num_bricks_total = m_bricks.size();
    size_t bricks_to_keep = static_cast<size_t>(num_bricks_total * m_quality);
    
    // Clamp to valid range
    if (bricks_to_keep > num_bricks_total) bricks_to_keep = num_bricks_total;

    // ---------------------------------------------------------
    // PHASE 6: Synthesis (Reconstruction)
    // ---------------------------------------------------------
    // Create new sparse grid initialized with the Background Value
    openvdb::FloatGrid::Ptr compressed_grid = openvdb::FloatGrid::create(m_background_value);
    compressed_grid->setTransform(m_grid->transform().copy());
    
    // Use Accessors for fast, thread-safe (in concurrent contexts) read/write
    auto target_acc = compressed_grid->getAccessor();
    auto source_acc = m_grid->getAccessor();

    // [cite: 196] Iterate over sorted list and only process the "kept" bricks
    const int BRICK_DIM = 32;

    for (size_t i = 0; i < bricks_to_keep; ++i) {
        const auto& brick = m_bricks[i];
        
        //  "Activate all the voxels... of that brick"
        // We iterate the local 32x32x32 space of the brick
        for (int z = 0; z < BRICK_DIM; ++z) {
            for (int y = 0; y < BRICK_DIM; ++y) {
                for (int x = 0; x < BRICK_DIM; ++x) {
                    // Calculate global coordinate
                    openvdb::Coord c = brick.origin.offsetBy(x, y, z);
                    
                    // Critical: We only copy if the source actually has data.
                    // This preserves the inherent sparsity of the original volume.
                    if (source_acc.isValueOn(c)) {
                        target_acc.setValue(c, source_acc.getValue(c));
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------
    // PHASE 7: Optimization
    // ---------------------------------------------------------
    //  Collapse constant branches to minimize memory footprint
    compressed_grid->tree().prune();

    // ---------------------------------------------------------
    // POST-COMPRESSION CHECK
    // ---------------------------------------------------------
    // Count active voxels in the new compressed volume
    uint64_t count_after = compressed_grid->activeVoxelCount();
    
    // Calculate actual compression ratio
    float ratio = 100.0f * static_cast<float>(count_after) / static_cast<float>(count_before);

    std::cout << "[VDB Compress] Compressed Active Voxels: " << count_after << std::endl;
    std::cout << "[VDB Compress] Actual Data Retention: " << ratio << "%" << std::endl;

    return compressed_grid;
}
