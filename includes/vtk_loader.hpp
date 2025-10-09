#ifndef VOXEL_LOADER_H
#define VOXEL_LOADER_H

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <glm/glm.hpp>  // GLM header for vec3, vec4, etc.

// A class to represent a loaded 3D voxel dataset.
class VoxelLoader {
public:
    struct Dimensions {
        size_t x = 0, y = 0, z = 0;
    };

    // --- Public Interface ---

    // Default constructor
    VoxelLoader() = default;

    // Loads a VTK file from the given path.
    // Throws std::runtime_error on failure.
    bool loadVTK(const std::string& filepath);

    // --- Public Getters ---
    // These methods provide safe, read-only access to the data.

    const std::vector<unsigned char>& getData() const { return m_data; }
    const Dimensions& getDimensions() const { return m_dimensions; }
    const glm::vec3& getOrigin() const { return m_origin; }
    const glm::vec3& getSpacing() const { return m_spacing; }
    size_t getTotalPoints() const { return m_totalPoints; }

private:
    // --- Private Member Variables ---

    std::vector<unsigned char> m_data; // Stores the raw voxel data (assuming uint8 for now)
    Dimensions m_dimensions;           // Dimensions of the voxel grid
    glm::vec3 m_origin;                // Physical origin of the dataset
    glm::vec3 m_spacing;               // Physical spacing between voxels
    size_t m_totalPoints = 0;          // Total number of points (width * height * depth)
    std::string m_dataType;            // Data type as a string (e.g., "unsigned_char")

    // --- Private Helper Methods ---

    // Resets all member variables to a default state.
    void reset();

    // Byte swaps a 16-bit short (for little-endian conversion).
    // Note: This needs to be expanded if you load other data types.
    void byteSwap16(unsigned short& value);
};

#endif // VOXEL_LOADER_H
