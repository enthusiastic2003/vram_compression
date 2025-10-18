#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "vtk_loader.hpp"
#include "camera.h"
#include "shader.h"
#include <memory>

class Renderer {
private:
    GLFWwindow* window;
    void renderScene();
    void renderUI();
    static void glfw_error_callback(int error, const char* description);
    std::shared_ptr<VoxelLoader> m_voxelLoader;
    Camera camera_;
    Shader m_shader;
    GLuint m_cubeVBO;
    GLuint m_cubeVAO;
    int width_;
    int height_;
    const char* title_;
    GLFWwindow* window_;
public:
    Renderer(int width, int height, const char* title)
        : width_(width), height_(height), title_(title),
          window_(nullptr),
          camera_(3.0f, 45.0f)  // distance=3, fov=45
    {}

    ~Renderer();

    bool initialize(std::shared_ptr<VoxelLoader>);
    void run();

private:
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPosition(double xpos, double ypos);
    void handleScroll(double xoffset, double yoffset);
    void handleKey(int key, int scancode, int action, int mods);

    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
    static void defaultKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};