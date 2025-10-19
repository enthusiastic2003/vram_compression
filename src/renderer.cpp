#include <iostream>
#include "renderer.hpp"
// Define this before including GLFW
#include <GLFW/glfw3.h>
// ImGui includes
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "vtk_loader.hpp"

// Renderer::Renderer(int width, int height, const char* title)
//     : width_(width), height_(height), title_(title),
//       window_(nullptr),
//       camera_(3.0f, 45.0f)  // distance=3, fov=45
// {}

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

    // --- NEW ---
    // Get initial framebuffer size and set the viewport
    glfwGetFramebufferSize(window, &width_, &height_);
    glViewport(0, 0, width_, height_);

    // Set up window user pointer and callbacks
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, defaultKeyCallback);

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

    // Create and compile our GLSL program from the shaders
    m_shader = Shader("shaders/proxy.vert", "shaders/proxy.fs");
    m_shader.compileAndLink();

    // A simple cube
    float vertices[] = {
        // positions         
        -0.5f, -0.5f, -0.5f, 
         0.5f, -0.5f, -0.5f,  
         0.5f,  0.5f, -0.5f,  
         0.5f,  0.5f, -0.5f,  
        -0.5f,  0.5f, -0.5f, 
        -0.5f, -0.5f, -0.5f, 

        -0.5f, -0.5f,  0.5f, 
         0.5f, -0.5f,  0.5f,  
         0.5f,  0.5f,  0.5f,  
         0.5f,  0.5f,  0.5f,  
        -0.5f,  0.5f,  0.5f, 
        -0.5f, -0.5f,  0.5f, 

        -0.5f,  0.5f,  0.5f, 
        -0.5f,  0.5f, -0.5f, 
        -0.5f, -0.5f, -0.5f, 
        -0.5f, -0.5f, -0.5f, 
        -0.5f, -0.5f,  0.5f, 
        -0.5f,  0.5f,  0.5f, 

         0.5f,  0.5f,  0.5f, 
         0.5f,  0.5f, -0.5f, 
         0.5f, -0.5f, -0.5f, 
         0.5f, -0.5f, -0.5f, 
         0.5f, -0.5f,  0.5f, 
         0.5f,  0.5f,  0.5f, 

        -0.5f, -0.5f, -0.5f, 
         0.5f, -0.5f, -0.5f, 
         0.5f, -0.5f,  0.5f, 
         0.5f, -0.5f,  0.5f, 
        -0.5f, -0.5f,  0.5f, 
        -0.5f, -0.5f, -0.5f, 

        -0.5f,  0.5f, -0.5f, 
         0.5f,  0.5f, -0.5f, 
         0.5f,  0.5f,  0.5f, 
         0.5f,  0.5f,  0.5f, 
        -0.5f,  0.5f,  0.5f, 
        -0.5f,  0.5f, -0.5f, 
    };

    glGenVertexArrays(1, &m_cubeVAO);
    glGenBuffers(1, &m_cubeVBO);

    glBindVertexArray(m_cubeVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // In Renderer.cpp, inside initialize()
// ... after setting up VAO/VBO

    // Get data from your loader
    const auto& dims = m_voxelLoader->getDimensions();
    // Get a pointer to the underlying vector data returned by the loader
    const auto& data_vec = m_voxelLoader->getData();
    const unsigned char* data_ptr = data_vec.data();

    glGenTextures(1, &m_volumeTextureID);
    glBindTexture(GL_TEXTURE_3D, m_volumeTextureID);

    // Set texture parameters (these are good)
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // --- CORRECTED TEXTURE UPLOAD ---
    glTexImage3D(
        GL_TEXTURE_3D,
        0,
        GL_R8,             // Internal format on GPU: 8-bit single channel
        dims.x,
        dims.y,
        dims.z,
        0,
        GL_RED,            // Format of source data: single channel
        GL_UNSIGNED_BYTE,  // Type of source data: unsigned char
        data_ptr
    );

    glBindTexture(GL_TEXTURE_3D, 0);

    // Position attribute
    glEnableVertexAttribArray(0);

    return true;
}

void Renderer::renderScene() {
    // 1. Clear the screen for the new frame
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); 

    // 2. Use the shader program
    m_shader.use();

    // Set camera and rendering uniforms
    m_shader.setMat4("view", camera_.getViewMatrix());
    m_shader.setMat4("projection", camera_.getProjectionMatrix( (float)width_ / (float)height_));
    m_shader.setMat4("model", glm::mat4(1.0f));
    m_shader.setFloat("u_stepSize", 0.005f);
    m_shader.setInt("u_marchSteps", 256);
    m_shader.setVec3("u_cameraPosition", camera_.getPosition().x, camera_.getPosition().y, camera_.getPosition().z);

    // Set transfer function uniforms
    m_shader.setVec3("u_color1", m_color1.x, m_color1.y, m_color1.z);
    m_shader.setVec3("u_color2", m_color2.x, m_color2.y, m_color2.z);
    m_shader.setFloat("u_alpha1", m_alpha1);
    m_shader.setFloat("u_alpha2", m_alpha2);
    m_shader.setFloat("u_threshold", m_threshold);

    // Bind the 3D Volume Texture to texture unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, m_volumeTextureID);
    m_shader.setInt("u_volumeTexture", 0);

    // --- MODIFICATIONS START HERE ---

    // 3. Set OpenGL state for transparent volume rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Standard alpha blending
    glDisable(GL_CULL_FACE); // This is the key: render both front and back faces
    glDepthMask(GL_FALSE);   // Don't let the volume write to the depth buffer

    // 4. Bind the cube's VAO and draw it
    glBindVertexArray(m_cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // 5. Restore OpenGL state to default
    glDepthMask(GL_TRUE);    // Re-enable depth writing
    glEnable(GL_CULL_FACE);  // Re-enable face culling for other objects (like ImGui)
    glDisable(GL_BLEND);

    // --- MODIFICATIONS END HERE ---

    // 6. Unbind everything
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_3D, 0); // Good practice to unbind texture
}

void Renderer::renderUI() {
    // 1. Start a new ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 2. Build your UI here
    ImGui::Begin("Transfer Function Editor");
    ImGui::ColorEdit3("Start Color", &m_color1.x);
    ImGui::ColorEdit3("End Color", &m_color2.x);
    ImGui::SliderFloat("Start Alpha", &m_alpha1, 0.0f, 1.0f);
    ImGui::SliderFloat("End Alpha", &m_alpha2, 0.0f, 1.0f);
    ImGui::SliderFloat("Threshold", &m_threshold, 0.0f, 1.0f);
    ImGui::End();

    camera_.renderImGuiControls();

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
    float lastFrame = 0.0f;
    // This is the main application loop.
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Handle user input and OS events
        glfwPollEvents();

        // Process keyboard input for camera
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            camera_.onKeyboard(GLFW_KEY_UP, GLFW_PRESS, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            camera_.onKeyboard(GLFW_KEY_DOWN, GLFW_PRESS, deltaTime);

        // Render the main 3D scene
        renderScene();

        // Render the user interface on top of the scene
        renderUI();

        // Swap the front and back buffers to display the rendered frame
        glfwSwapBuffers(window);
    }
}

void Renderer::mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer)
        renderer->handleMouseButton(button, action, mods);
}

void Renderer::cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer)
        renderer->handleCursorPosition(xpos, ypos);
}

void Renderer::scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer)
        renderer->handleScroll(xoffset, yoffset);
}

void Renderer::defaultKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer)
        renderer->handleKey(key, scancode, action, mods);
}

void Renderer::handleMouseButton(int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    camera_.onMouseButton(button, action, xpos, ypos);
}

void Renderer::handleCursorPosition(double xpos, double ypos) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    camera_.onMouseMove(xpos, ypos);
}

void Renderer::handleScroll(double xoffset, double yoffset) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    camera_.onScroll(yoffset);
}

void Renderer::handleKey(int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

// Add this static callback function with the others
void Renderer::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->handleFramebufferSizeChange(width, height);
    }
}

// Add this handler method with the other handlers
void Renderer::handleFramebufferSizeChange(int width, int height) {
    // This is the crucial line!
    glViewport(0, 0, width, height);

    // Update our stored width and height
    width_ = width;
    height_ = height;
}