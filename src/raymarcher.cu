#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <surface_indirect_functions.h> // For surface writing
// src/raymarch.cu
#include <nanovdb/cuda/DeviceBuffer.h>
#include <nanovdb/GridHandle.h>
//CudaDeviceBuffer.h>

using DeviceHandle = nanovdb::GridHandle<nanovdb::cuda::DeviceBuffer>;

// 1. The Kernel
// Writes a gradient pattern based on pixel coordinates
__global__ void DummyWriteKernel(cudaSurfaceObject_t surface, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    // Calculate normalized coordinates [0, 1] for color
    float r = (float)x / width;
    float g = (float)y / height;
    float b = 0.0f;
    float a = 1.0f;

    // Create a float4 color
    float4 pixelColor = make_float4(r, g, b, a);

    // Write to the surface at (x, y)
    // surf2Dwrite(data, surfaceObject, x_in_bytes, y)
    surf2Dwrite(pixelColor, surface, x * sizeof(float4), y);
}

// 2. The Wrapper (Callable from C++)
extern "C" void LaunchDummyKernel(cudaSurfaceObject_t surface, int width, int height) {
    dim3 blockSize(16, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, 
                  (height + blockSize.y - 1) / blockSize.y);

    DummyWriteKernel<<<gridSize, blockSize>>>(surface, width, height);
}


__global__ void VerifyKernel(const nanovdb::FloatGrid* grid) {
    // Only let thread (0,0) print the info
    if (threadIdx.x == 0 && blockIdx.x == 0) {
        printf("\n=== GPU DATA VERIFICATION ===\n");
        
        if (!grid) {
            printf("ERROR: Grid pointer is NULL!\n");
            return;
        }

        // 1. Check Magic Number (The ultimate validity test)
        if (!grid->isValid()) {
            printf("ERROR: Grid structure is corrupt (Magic Number mismatch).\n");
            return;
        }

        // 2. Read Grid Name (Stored in VRAM)
        // printf can read strings from device memory
        printf("Grid Name: %s\n", grid->gridName());

        // 3. Read Tree Stats
        auto& tree = grid->tree();
        nanovdb::CoordBBox bbox = tree.bbox();
        
        printf("Grid Type: Float (Verified)\n");
        printf("Active Voxel Count: %llu\n", tree.activeVoxelCount());
        // printf("Active Tiles: %llu\n", tree.activeTileCount());
        
        printf("Bounding Box Index Space:\n");
        printf("   Min: (%d, %d, %d)\n", bbox.min()[0], bbox.min()[1], bbox.min()[2]);
        printf("   Max: (%d, %d, %d)\n", bbox.max()[0], bbox.max()[1], bbox.max()[2]);
        
        printf("Memory Check: SUCCESS\n");
        printf("=============================\n\n");
    }
}

// src/raymarcher.cu

extern "C" void FreeVDB(void* handlePtr) {
    if (handlePtr) {
        // 1. Cast the opaque void* back to our specific Handle type
        DeviceHandle* handle = static_cast<DeviceHandle*>(handlePtr);
        
        // 2. Delete the object
        // This calls ~GridHandle(), which calls ~DeviceBuffer()
        // ~DeviceBuffer() automatically calls cudaFree() on the VRAM.
        delete handle;
    }
}

extern "C" const nanovdb::FloatGrid* GetDeviceGrid(void* handlePtr) {
    if (!handlePtr) return nullptr;
    
    // Cast back to the real type
    DeviceHandle* handle = static_cast<DeviceHandle*>(handlePtr);
    
    // Ask the handle for the raw GPU pointer to the grid structure
    return handle->deviceGrid<float>();
}

extern "C" void* AllocAndUploadVDB(const void* hostData, uint64_t size, cudaStream_t stream) {
    // 1. Allocate VRAM
    // We use the factory method: create(size, dummy, device_id, stream)
    // passing '0' for the device ID to force GPU allocation.
    auto deviceBuffer = nanovdb::cuda::DeviceBuffer::create(size, nullptr, 0, stream);

    // 2. Manual Copy
    // Your DeviceBuffer class exposes the raw GPU pointer via deviceData().
    // We use standard CUDA to copy our bytes there.
    void* gpuPtr = deviceBuffer.deviceData();
    
    cudaMemcpyAsync(gpuPtr, hostData, size, cudaMemcpyHostToDevice, stream);

    // 3. Wrap and Return
    // Move the populated buffer into the Handle
    DeviceHandle* handle = new DeviceHandle(std::move(deviceBuffer));
    return (void*)handle;
}

// The C-Wrapper
extern "C" void VerifyVDB(void* handlePtr) {
    // 1. Get the specific pointer
    const nanovdb::FloatGrid* grid = GetDeviceGrid(handlePtr);
    
    // 2. Launch a single thread to inspect it
    VerifyKernel<<<1, 1>>>(grid);
    
    // 3. Wait for print to finish (printf buffer flush)
    cudaDeviceSynchronize();
}