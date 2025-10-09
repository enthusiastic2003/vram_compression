#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "renderer.hpp"
// ImGui includes
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

static void glfw_error_callback(int error, const char* description)
{
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

int main()
{
    Renderer renderer;
    if (renderer.initialize() == false) {
        std::cerr << "Failed to initialize renderer\n";
        return -1;
    }
    renderer.run();

    return 0;
}
