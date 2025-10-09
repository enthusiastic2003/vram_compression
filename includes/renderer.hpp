#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
class Renderer {
private:
    GLFWwindow* window;
    // New private helper methods for rendering
    void renderScene();
    void renderUI();
    // void renderFrame();
    static void glfw_error_callback(int error, const char* description);
public:
    Renderer();
    ~Renderer();


    bool initialize();
    void run();
};