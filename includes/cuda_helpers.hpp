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
    
    // The Ray Marcher Launcher
    void LaunchRayMarch(
        cudaSurfaceObject_t surface, 
        int width, int height, 
        void* handle,                       // <--- UPDATED: Opaque Pointer
        cudaTextureObject_t tfTexture, // <--- ADD THIS PARAMETER
        float cX, float cY, float cZ,       // Camera Position
        float dX, float dY, float dZ,       // Camera Direction
        float uX, float uY, float uZ,       // Camera Up
        float rX, float rY, float rZ,       // Camera Right
        float fov                           // FOV
    );
}