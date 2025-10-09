#include <iostream>
#include "renderer.hpp"
#include "vtk_loader.hpp"
#include <memory>
int main()
{
    auto voxelData = std::make_shared<VoxelLoader>();
    if (!voxelData->loadVTK("data/vtk/bonsai_256x256x256_uint8.vtk")) {
        std::cerr << "Failed to load VTK file\n";
        return -1;
    }

    Renderer renderer;
    if (renderer.initialize(voxelData) == false) {
        std::cerr << "Failed to initialize renderer\n";
        return -1;
    }
    renderer.run();

    return 0;
}
