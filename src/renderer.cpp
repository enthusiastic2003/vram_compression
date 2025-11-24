#include <iostream>
#include "renderer.hpp"
// Define this before including GLFW
#include <GLFW/glfw3.h>
// ImGui includes
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "vtk_loader.hpp"
#include <openvdb/openvdb.h>
#include <nanovdb/util/CreateNanoGrid.h>
#include <nanovdb/util/IO.h>
#include "cuda_helpers.hpp"
#include "vdb_compressor.h"


Renderer::~Renderer() {
    FreeVDB(m_deviceHandle);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();

}

void Renderer::glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

bool Renderer::initialize(std::shared_ptr<VoxelLoader> loader) {
    // Voxel Processing
    m_voxelLoader = loader;
    if (m_voxelLoader->getTotalPoints() == 0) {
        std::cerr << "Voxel data is empty or not loaded properly.\n";
        return false;
    }

    // Initialize GLFW
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

    window = glfwCreateWindow(1280, 720, "CUDA Volume Ray Marcher", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        return false;
    }

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
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    // Enable Docking + Multi-Viewport
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    // When viewports are enabled, tweak style for consistency across OS windows
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Create simple shader for displaying the CUDA-generated texture
    m_shader = Shader("shaders/proxy.vert", "shaders/proxy.fs");
    m_shader.compileAndLink();

    // OpenVDB tree initialization
    openvdb::initialize();
    openvdb::FloatGrid::Ptr m_grid = openvdb::FloatGrid::create(/*background value=*/0.0f);

    // Get grid accessor
    openvdb::FloatGrid::Accessor m_accessor = m_grid->getAccessor();

    const auto& dims = m_voxelLoader->getDimensions();
    const auto& rawData = m_voxelLoader->getData();
    size_t totalSize = rawData.size();

    // Populate OpenVDB grid with voxel data
    // Loop order: Z -> Y -> X for cache efficiency
    for (int k = 0; k < dims.z; ++k) {
        for (int j = 0; j < dims.y; ++j) {
            for (int i = 0; i < dims.x; ++i) {
                size_t index = (size_t)k * (dims.x * dims.y) + (size_t)j * dims.x + (size_t)i;
                
                if (index < totalSize) {
                    uint8_t val = rawData[index];
                    
                    if (val > 0) {
                        float density = static_cast<float>(val) / 255.0f;
                        m_accessor.setValue(openvdb::Coord(i, j, k), density);
                    }
                }
            }
        }
    }

    // Compress the VDB grid using our vdb_compressor
    float compressionQuality = 0.5f; // User-defined quality parameter [0.0 - 1.0]
    vdb_compressor compressor(m_grid, compressionQuality);
    m_grid = compressor.compress("f2"); // Using f2 similarity metric


    // Convert to NanoVDB for CUDA
    m_grid->setName("My Voxel Grid");
    auto handle = nanovdb::tools::createNanoGrid<openvdb::FloatGrid, float, nanovdb::cuda::DeviceBuffer>(*m_grid);

    std::cout << "\n=== HOST DATA VERIFICATION (OpenVDB) ===\n";
    std::cout << "Grid Name: " << m_grid->getName() << "\n";
    std::cout << "Grid Type: " << m_grid->type() << "\n";
    std::cout << "Active Voxel Count: " << m_grid->activeVoxelCount() << "\n";

    openvdb::CoordBBox bbox = m_grid->evalActiveVoxelBoundingBox();
    std::cout << "Bounding Box Index Space:\n";
    std::cout << "   Min: (" << bbox.min().x() << ", " << bbox.min().y() << ", " << bbox.min().z() << ")\n";
    std::cout << "   Max: (" << bbox.max().x() << ", " << bbox.max().y() << ", " << bbox.max().z() << ")\n";
    std::cout << "========================================\n\n";

    // Upload to CUDA device
    cudaStream_t stream;
    cudaStreamCreate(&stream);

    m_deviceHandle = AllocAndUploadVDB(handle.data(), handle.size(), stream);
    VerifyVDB(m_deviceHandle);

    cudaStreamSynchronize(stream);
    cudaStreamDestroy(stream);

    // Initialize rendering components
    initQuad();
    initCudaInterop();

    // Set up camera to view the volume
    camera_.setTarget(glm::vec3(128.0f, 128.0f, 128.0f));
    camera_.setDistance(400.0f);

    return true;
}

void Renderer::initQuad() {
    float quadVertices[] = { 
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}

void Renderer::initCudaInterop() {
    // Create OpenGL texture for CUDA output
    glGenTextures(1, &m_cudaOutputTex);
    glBindTexture(GL_TEXTURE_2D, m_cudaOutputTex);
    
    // RGBA32F is standard for CUDA interop
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width_, height_, 0, GL_RGBA, GL_FLOAT, NULL);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Register texture with CUDA
    cudaError_t err = cudaGraphicsGLRegisterImage(&m_cudaResource, m_cudaOutputTex, 
                                                  GL_TEXTURE_2D, 
                                                  cudaGraphicsRegisterFlagsWriteDiscard);
    
    if (err != cudaSuccess) {
        std::cerr << "CUDA texture mapping failed: " << cudaGetErrorString(err) << std::endl;
    }
}

void Renderer::renderScene() {
    // Map OpenGL texture to CUDA
    cudaGraphicsMapResources(1, &m_cudaResource, 0);
    
    cudaArray_t array;
    cudaGraphicsSubResourceGetMappedArray(&array, m_cudaResource, 0, 0);

    cudaResourceDesc resDesc;
    memset(&resDesc, 0, sizeof(resDesc));
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = array;

    cudaSurfaceObject_t surface;
    cudaCreateSurfaceObject(&surface, &resDesc);

    // Get camera parameters
    glm::vec3 cPos = camera_.getPosition();
    glm::vec3 cDir = camera_.getDirection();
    glm::vec3 cUp = camera_.getUp();
    glm::vec3 cRight = glm::normalize(glm::cross(cDir, cUp));
    glm::vec3 cLocalUp = glm::normalize(glm::cross(cRight, cDir));
    float fovRad = glm::radians(camera_.getFOV());

    // Launch CUDA ray marching kernel
    LaunchRayMarch(
        surface, width_, height_, 
        m_deviceHandle,
        cPos.x, cPos.y, cPos.z,
        cDir.x, cDir.y, cDir.z,
        cLocalUp.x, cLocalUp.y, cLocalUp.z,
        cRight.x, cRight.y, cRight.z,
        fovRad
    );

    // Cleanup CUDA resources
    cudaDestroySurfaceObject(surface);
    cudaGraphicsUnmapResources(1, &m_cudaResource, 0);

    // Render CUDA output to screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_cudaOutputTex);
    m_shader.setInt("screenTexture", 0);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::renderUI() {
    // Start new ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Build UI
    ImGui::Begin("Transfer Function Editor");
    ImGui::ColorEdit3("Start Color", &m_color1.x);
    ImGui::ColorEdit3("End Color", &m_color2.x);
    ImGui::SliderFloat("Start Alpha", &m_alpha1, 0.0f, 1.0f);
    ImGui::SliderFloat("End Alpha", &m_alpha2, 0.0f, 1.0f);
    ImGui::SliderFloat("Threshold", &m_threshold, 0.0f, 1.0f);
    ImGui::End();

    camera_.renderImGuiControls();

    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Handle multiple OS windows
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
    
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();

        // Process keyboard input for camera
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
            camera_.onKeyboard(GLFW_KEY_UP, GLFW_PRESS, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
            camera_.onKeyboard(GLFW_KEY_DOWN, GLFW_PRESS, deltaTime);

        // Render scene and UI
        renderScene();
        renderUI();

        glfwSwapBuffers(window);
    }
}

// GLFW Callbacks
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

void Renderer::framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    Renderer* renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->handleFramebufferSizeChange(width, height);
    }
}

// Event Handlers
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

void Renderer::handleFramebufferSizeChange(int width, int height) {
    if (width == 0 || height == 0) return; // Window is minimized

    glViewport(0, 0, width, height);
    width_ = width;
    height_ = height;

    // --- RESIZE LOGIC ---
    
    // 1. Unregister the old resource from CUDA
    // We MUST do this before touching the OpenGL texture, otherwise CUDA holds a lock on dead memory.
    if (m_cudaResource) {
        cudaGraphicsUnregisterResource(m_cudaResource);
    }

    // 2. Resize the OpenGL Texture
    // We allocate new storage for the texture with the new dimensions.
    glBindTexture(GL_TEXTURE_2D, m_cudaOutputTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 3. Re-register with CUDA
    // Map the new, larger texture to the CUDA resource handle.
    cudaError_t err = cudaGraphicsGLRegisterImage(
        &m_cudaResource, 
        m_cudaOutputTex, 
        GL_TEXTURE_2D, 
        cudaGraphicsRegisterFlagsWriteDiscard
    );

    if (err != cudaSuccess) {
        std::cerr << "CUDA Resize failed: " << cudaGetErrorString(err) << std::endl;
    }
}