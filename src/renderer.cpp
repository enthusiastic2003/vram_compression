#include <iostream>
#include "renderer.hpp"
// Define this before including GLFW
#include <GLFW/glfw3.h>
// ImGui includes
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "vtk_loader.hpp"
Renderer::Renderer() : window(nullptr) {
    // Constructor now ensures 'window' is safely initialized
}

// The destructor is the best place for cleanup code.
Renderer::~Renderer() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}


void Renderer::glfw_error_callback(int error, const char* description)
{
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

bool Renderer::initialize(std::shared_ptr<VoxelLoader> loader) {
    // Voxel Processing
    m_voxelLoader = loader;
    if (m_voxelLoader->getTotalPoints() == 0) {
        std::cerr << "Voxel data is empty or not loaded properly.\n";
        return false;
    }

    

    // Initialization code (e.g., setting up OpenGL context, shaders, etc.)
   glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return false;

    // OpenGL 3.3 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window = glfwCreateWindow(1280, 720, "GLFW + GLAD + ImGui", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD\n";
        return false;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Enable Docking + Multi-Viewport
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;        // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;      // Enable Multi-Viewport / Platform Windows

    ImGui::StyleColorsDark();

    // When viewports are enabled, tweak style for consistency across OS windows
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");


    return true;

}

void Renderer::renderScene() {
    // 1. Clear the screen for the new frame
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 2. All your future scene rendering code (e.g., ray marching) will go here.
}

void Renderer::renderUI() {
    // 1. Start a new ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 2. Build your UI here
    // Example: Show a demo window
    static bool show_demo = true;
    if (show_demo) {
        ImGui::ShowDemoWindow(&show_demo);
    }
    // You can add other ImGui windows or controls here.

    // 3. Render the ImGui frame
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Handle multiple OS windows (if docking is enabled)
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}

void Renderer::run() {
    // This is the main application loop.
    while (!glfwWindowShouldClose(window)) {
        // Handle user input and OS events
        glfwPollEvents();

        // Render the main 3D scene
        renderScene();

        // Render the user interface on top of the scene
        renderUI();

        // Swap the front and back buffers to display the rendered frame
        glfwSwapBuffers(window);
    }
}