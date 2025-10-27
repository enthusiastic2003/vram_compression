#include <openvdb/openvdb.h>
#include <openvdb/io/File.h>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <input.vdb>" << std::endl;
        return 1;
    }
    
    openvdb::initialize();
    
    try {
        openvdb::io::File file(argv[1]);
        file.open();
        
        openvdb::GridBase::Ptr baseGrid;
        for (openvdb::io::File::NameIterator nameIter = file.beginName(); 
             nameIter != file.endName(); ++nameIter) {
            baseGrid = file.readGrid(nameIter.gridName());
            break;
        }
        file.close();
        
        if (auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(baseGrid)) {
            std::cout << "=== VDB File Info ===" << std::endl;
            std::cout << "Grid name: " << grid->getName() << std::endl;
            std::cout << "Grid class: " << grid->getGridClass() << std::endl;
            std::cout << "Memory usage: " << grid->memUsage() << " bytes" << std::endl;
            std::cout << "Active voxels: " << grid->activeVoxelCount() << std::endl;
            std::cout << "Background value: " << grid->background() << std::endl;
        } else {
            std::cout << "Error: Could not read grid as FloatGrid" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading VDB file: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
