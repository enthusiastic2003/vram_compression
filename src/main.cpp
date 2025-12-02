#include <iostream>
#include "renderer.hpp"
#include "vtk_loader.hpp"
#include <memory>
int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_vtk_file>" << std::endl;
        return -1;
    }

    const char* vtkFilePath = argv[1];

    auto voxelData = std::make_shared<VoxelLoader>();
    if (!voxelData->loadVTK(vtkFilePath)) {
        std::cerr << "Failed to load VTK file: " << vtkFilePath << std::endl;
        return -1;
    }

    Renderer renderer( std::string(vtkFilePath) ,1280, 900, "Voxel Renderer");
    if (renderer.initialize(voxelData) == false) {
        std::cerr << "Failed to initialize renderer\n";
        return -1;
    }
    renderer.run();

    return 0;
}
