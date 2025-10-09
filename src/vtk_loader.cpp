#include "vtk_loader.h"

void VoxelLoader::reset() {
    m_data.clear();
    m_dimensions = {0, 0, 0};
    m_origin = glm::vec3(0.0f);
    m_spacing = glm::vec3(1.0f);
    m_totalPoints = 0;
    m_dataType = "";
}

// Simple byte swap for a 16-bit value.
// The VTK data is big-endian, so we swap for little-endian machines (most PCs).
void VoxelLoader::byteSwap16(unsigned short& value) {
    value = (value >> 8) | (value << 8);
}
// Note: You would add byteSwap32 for floats/ints if needed.

bool VoxelLoader::loadVTK(const std::string& filepath) {
    reset(); // Clear previous data

    std::ifstream file(filepath, std::ios::binary); // Open binary mode for generality
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file: " << filepath << std::endl;
        return false;
    }

    std::string line;
    std::string format;

    try {
        // 1. Read header
        std::getline(file, line); // version
        std::getline(file, line); // title
        std::getline(file, line); // format (ASCII/BINARY)
        format = line;
        if (line.find("BINARY") != std::string::npos) {
            format = "BINARY";
        } else if (line.find("ASCII") != std::string::npos) {
            format = "ASCII";
        } else {
            throw std::runtime_error("Unsupported format: " + line);
        }

        // Read metadata lines
        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string keyword;
            ss >> keyword;

            if (keyword == "DIMENSIONS") {
                ss >> m_dimensions.x >> m_dimensions.y >> m_dimensions.z;
            } else if (keyword == "ORIGIN") {
                ss >> m_origin.x >> m_origin.y >> m_origin.z;
            } else if (keyword == "SPACING") {
                ss >> m_spacing.x >> m_spacing.y >> m_spacing.z;
            } else if (keyword == "POINT_DATA") {
                ss >> m_totalPoints;
            } else if (keyword == "SCALARS") {
                std::string name;
                ss >> name >> m_dataType;
            } else if (keyword == "LOOKUP_TABLE") {
                break; // End of header for binary ASCII SCALARS
            } else if (keyword == "FIELD") {
                // Handle FIELD data (e.g., double arrays)
                std::string fieldName;
                int numComponents, numTuples;
                ss >> fieldName >> numComponents >> numTuples;
                m_dataType = "double"; // Override for FIELD double data
                break;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error parsing VTK header: " << e.what() << std::endl;
        return false;
    }

    // 2. Sanity check
    if (m_totalPoints == 0) {
        std::cerr << "Error: No data points found in file." << std::endl;
        return false;
    }

    // 3. Read the data conditionally
    m_data.clear();
    if (format == "BINARY") {
        if (m_dataType == "unsigned_char" || m_dataType == "uint8") {
            m_data.resize(m_totalPoints);
            file.read(reinterpret_cast<char*>(m_data.data()), m_totalPoints);
        } else if (m_dataType == "unsigned_short") {
            std::vector<unsigned short> short_data(m_totalPoints);
            file.read(reinterpret_cast<char*>(short_data.data()), m_totalPoints * sizeof(unsigned short));
            for (auto& val : short_data) byteSwap16(val);
            // Convert to unsigned char if desired
            m_data.resize(m_totalPoints);
            for (size_t i = 0; i < m_totalPoints; ++i)
                m_data[i] = static_cast<unsigned char>(short_data[i]);
        } else if (m_dataType == "double") {
            std::vector<double> dbl_data(m_totalPoints);
            file.read(reinterpret_cast<char*>(dbl_data.data()), m_totalPoints * sizeof(double));
            m_data.resize(m_totalPoints);
            for (size_t i = 0; i < m_totalPoints; ++i)
                m_data[i] = static_cast<unsigned char>(dbl_data[i]);
        } else {
            std::cerr << "Unsupported binary data type: " << m_dataType << std::endl;
            return false;
        }
    } else if (format == "ASCII") {
        m_data.resize(m_totalPoints);
        size_t count = 0;
        double val;
        while (count < m_totalPoints && file >> val) {
            // Normalize/cast to unsigned char
            m_data[count++] = static_cast<unsigned char>(val);
        }
        if (count != m_totalPoints) {
            std::cerr << "Warning: expected " << m_totalPoints << " points, but read " << count << std::endl;
        }
    }
    std::cout << "=== VTK File Info ===" << std::endl;
    std::cout << "Dimensions: "
            << m_dimensions.x << " x "
            << m_dimensions.y << " x "
            << m_dimensions.z << std::endl;

    std::cout << "Origin: ("
            << m_origin.x << ", "
            << m_origin.y << ", "
            << m_origin.z << ")" << std::endl;

    std::cout << "Spacing: ("
            << m_spacing.x << ", "
            << m_spacing.y << ", "
            << m_spacing.z << ")" << std::endl;

    std::cout << "Total points: " << m_totalPoints << std::endl;
    std::cout << "Data type: " << m_dataType << std::endl;
    std::cout << "Data vector size: " << m_data.size() << std::endl;

    // Optionally print first few values to check content
    std::cout << "First 10 voxel values: ";
    for (size_t i = 0; i < std::min(size_t(10), m_data.size()); ++i) {
        std::cout << static_cast<int>(m_data[i]) << " ";
    }
    std::cout << std::endl << "======================";
    std::cout << std::endl;
    
    return true;
}



