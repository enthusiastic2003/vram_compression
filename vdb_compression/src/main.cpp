#include "VDBCompressor.h"
#include <openvdb/openvdb.h>
#include <openvdb/io/File.h>
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <input.vtk> [quality=0.5] [output.vdb] [metric=3]" << std::endl;
        std::cout << "Quality: 0.1 (high compression) to 1.0 (low compression)" << std::endl;
        std::cout << "Similarity metrics: 1=closest, 2=farthest, 3=median (recommended)" << std::endl;
        return 1;
    }
    
    std::string inputFile = argv[1];
    float quality = (argc > 2) ? std::atof(argv[2]) : 0.5f;
    std::string outputFile = (argc > 3) ? argv[3] : "output.vdb";
    int metricType = (argc > 4) ? std::atoi(argv[4]) : 3;
    
    try {
        VDBCompressor compressor;
        
        std::cout << "=== OpenVDB Compression ===" << std::endl;
        std::cout << "Input: " << inputFile << std::endl;
        std::cout << "Quality: " << quality << std::endl;
        std::cout << "Output: " << outputFile << std::endl;
        std::cout << "Similarity metric: " << metricType << std::endl;
        
        auto compressedGrid = compressor.compressVTKVolume(inputFile, quality, 32, metricType);
        
        openvdb::io::File file(outputFile);
        openvdb::GridPtrVec grids;
        grids.push_back(compressedGrid);
        file.write(grids);
        file.close();
        
        std::cout << "✓ Compression completed successfully!" << std::endl;
        std::cout << "Output saved to: " << outputFile << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
