#pragma once
#include <stdint.h> 

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <surface_indirect_functions.h> // For surface writing
extern "C" void LaunchDummyKernel(cudaSurfaceObject_t surface, int width, int height);

// We use void* to pass the handle back and forth blindly
extern "C" {
    void* AllocAndUploadVDB(const void* hostData, uint64_t size, cudaStream_t stream);
    void FreeVDB(void* handle);
    const nanovdb::FloatGrid* GetDeviceGrid(void* handlePtr);
    void VerifyVDB(void* handlePtr);
    
    // // Update your launcher signature to take void* handle
    // void LaunchRayMarch(
    //     cudaSurfaceObject_t surface, 
    //     int width, int height, 
    //     void* deviceHandle, // <--- Changed from Grid* to void*
    //     float cX, float cY, float cZ,
    //     float dX, float dY, float dZ,
    //     float uX, float uY, float uZ,
    //     float rX, float rY, float rZ,
    //     float fov
    // );
}