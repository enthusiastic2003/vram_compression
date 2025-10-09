#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "vtk_loader.hpp"
#include <memory>
class Renderer {
private:
    GLFWwindow* window;
    // New private helper methods for rendering
    void renderScene();
    void renderUI();
    // void renderFrame();
    static void glfw_error_callback(int error, const char* description);
    std::shared_ptr<VoxelLoader> m_voxelLoader;

public:
    Renderer();
    ~Renderer();


    bool initialize(std::shared_ptr<VoxelLoader>);
    void run();
};