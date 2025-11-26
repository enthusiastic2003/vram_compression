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

// Add these includes at the top
#include <algorithm>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

Renderer::~Renderer() {
    FreeVDB(m_deviceHandle);
    if (m_tfTexture) {
        glDeleteTextures(1, &m_tfTexture);
    }
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
    initTransferFunction();

    // Set up camera to view the volume
    camera_.setTarget(glm::vec3(128.0f, 128.0f, 128.0f));
    camera_.setDistance(400.0f);

    return true;
}

void Renderer::initTransferFunction() {
    // Initialize with default points
    m_tfPoints.clear();
    m_tfPoints.push_back(TFPoint(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
    m_tfPoints.push_back(TFPoint(0.3f, 1.0f, 0.0f, 0.0f, 0.3f));
    m_tfPoints.push_back(TFPoint(0.6f, 1.0f, 1.0f, 0.0f, 0.7f));
    m_tfPoints.push_back(TFPoint(1.0f, 1.0f, 1.0f, 1.0f, 1.0f));
    
    // Generate histogram from voxel data
    GenerateHistogram();
    
    // Create transfer function texture
    glGenTextures(1, &m_tfTexture);
    updateTransferFunctionTexture();
}

void Renderer::GenerateHistogram() {
    const auto& rawData = m_voxelLoader->getData();
    m_histogram.resize(256, 0.0f);
    
    for (uint8_t val : rawData) {
        m_histogram[val]++;
    }
    
    // Normalize histogram
    float max_val = *std::max_element(m_histogram.begin(), m_histogram.end());
    if (max_val > 0) {
        for (auto& val : m_histogram) {
            val /= max_val;
        }
    }
}

void Renderer::updateTransferFunctionTexture() {
    std::vector<float> textureData(256 * 4);
    
    for (int i = 0; i < 256; ++i) {
        float value = i / 255.0f;
        glm::vec4 color = SampleTF(value);
        textureData[i * 4 + 0] = color.r;
        textureData[i * 4 + 1] = color.g;
        textureData[i * 4 + 2] = color.b;
        textureData[i * 4 + 3] = color.a;
    }
    
    glBindTexture(GL_TEXTURE_1D, m_tfTexture);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, 256, 0, GL_RGBA, GL_FLOAT, textureData.data());
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_1D, 0);
}

glm::vec4 Renderer::SampleTF(float value) const {
    if (m_tfPoints.empty()) return glm::vec4(0, 0, 0, 0);
    if (value <= m_tfPoints.front().position) 
        return glm::vec4(m_tfPoints.front().red, m_tfPoints.front().green, m_tfPoints.front().blue, m_tfPoints.front().alpha);
    if (value >= m_tfPoints.back().position) 
        return glm::vec4(m_tfPoints.back().red, m_tfPoints.back().green, m_tfPoints.back().blue, m_tfPoints.back().alpha);
    
    for (size_t i = 0; i < m_tfPoints.size() - 1; ++i) {
        if (value >= m_tfPoints[i].position && value <= m_tfPoints[i+1].position) {
            float t = (value - m_tfPoints[i].position) / (m_tfPoints[i+1].position - m_tfPoints[i].position);
            return glm::vec4(
                m_tfPoints[i].red + t * (m_tfPoints[i+1].red - m_tfPoints[i].red),
                m_tfPoints[i].green + t * (m_tfPoints[i+1].green - m_tfPoints[i].green),
                m_tfPoints[i].blue + t * (m_tfPoints[i+1].blue - m_tfPoints[i].blue),
                m_tfPoints[i].alpha + t * (m_tfPoints[i+1].alpha - m_tfPoints[i].alpha)
            );
        }
    }
    
    return glm::vec4(m_tfPoints.back().red, m_tfPoints.back().green, m_tfPoints.back().blue, m_tfPoints.back().alpha);
}

void Renderer::AddTFPoint(float position) {
    glm::vec4 color = SampleTF(position);
    m_tfPoints.push_back(TFPoint(position, color.r, color.g, color.b, color.a));
    SortTFPoints();
    updateTransferFunctionTexture();
}

void Renderer::DeleteTFPoint(int index) {
    if (m_tfPoints.size() > 2) {
        m_tfPoints.erase(m_tfPoints.begin() + index);
        m_selectedPoint = -1;
        updateTransferFunctionTexture();
    }
}

void Renderer::SortTFPoints() {
    std::sort(m_tfPoints.begin(), m_tfPoints.end());
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
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    DrawTransferFunctionEditor();
    camera_.renderImGuiControls();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}

void Renderer::DrawTransferFunctionEditor() {
    ImGui::Begin("🎨 Transfer Function Editor", nullptr, ImGuiWindowFlags_NoCollapse);
    
    // Header with stats
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Volume Color Mapping");
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    ImGui::Text("Points: %d", (int)m_tfPoints.size());
    
    ImGui::Separator();
    
    // Main layout
    if (ImGui::BeginTable("TF_Layout", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableNextColumn();
        DrawHistogramAndGradient();
        
        ImGui::TableNextColumn();
        DrawChannelControls();
        
        ImGui::EndTable();
    }
    
    ImGui::Separator();
    
    // Bottom panels
    if (ImGui::BeginTable("TF_Panels", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableNextColumn();
        DrawPointProperties();
        
        ImGui::TableNextColumn();
        DrawPresetsPanel();
        
        ImGui::EndTable();
    }
    
    ImGui::End();
}

void Renderer::DrawHistogramAndGradient() {
    ImGui::Text("Histogram & Gradient");
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y = 200.0f;
    
    // Draw background
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
                            IM_COL32(25, 25, 25, 255));
    
    // Draw histogram
    if (!m_histogram.empty()) {
        for (size_t i = 0; i < m_histogram.size(); ++i) {
            float x = canvas_pos.x + (i / float(m_histogram.size())) * canvas_size.x;
            float width = canvas_size.x / m_histogram.size();
            float height = m_histogram[i] * canvas_size.y * 0.3f; // Use 30% of height for histogram
            
            draw_list->AddRectFilled(
                ImVec2(x, canvas_pos.y + canvas_size.y - height),
                ImVec2(x + width, canvas_pos.y + canvas_size.y),
                IM_COL32(100, 100, 255, 80)
            );
        }
    }
    
    // Draw gradient
    for (int i = 0; i < 256; ++i) {
        float t = i / 255.0f;
        glm::vec4 color = SampleTF(t);
        float x = canvas_pos.x + t * canvas_size.x;
        float x2 = canvas_pos.x + ((i + 1) / 255.0f) * canvas_size.x;
        
        draw_list->AddRectFilled(
            ImVec2(x, canvas_pos.y),
            ImVec2(x2, canvas_pos.y + canvas_size.y),
            ImColor(color.r, color.g, color.b, color.a)
        );
    }
    
    DrawControlPoints();
    
    // Draw border
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
                      IM_COL32(100, 100, 100, 255));
    
    ImGui::Dummy(canvas_size);
}

void Renderer::DrawControlPoints() {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y = 200.0f;
    
    // Handle interactions
    ImGui::InvisibleButton("tf_canvas", canvas_size);
    
    if (ImGui::IsItemHovered()) {
        // Add point on double click
        if (ImGui::IsMouseDoubleClicked(0)) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            float position = (mouse_pos.x - canvas_pos.x) / canvas_size.x;
            position = std::clamp(position, 0.0f, 1.0f);
            AddTFPoint(position);
        }
        
        // Select point
        if (ImGui::IsMouseClicked(0)) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            m_selectedPoint = -1;
            
            for (size_t i = 0; i < m_tfPoints.size(); ++i) {
                float x = canvas_pos.x + m_tfPoints[i].position * canvas_size.x;
                float y = canvas_pos.y + (1.0f - m_tfPoints[i].alpha) * canvas_size.y;
                
                float dx = mouse_pos.x - x;
                float dy = mouse_pos.y - y;
                
                if (dx * dx + dy * dy < 36.0f) {
                    m_selectedPoint = i;
                    m_isDragging = true;
                    break;
                }
            }
        }
    }
    
    // Drag points
    if (m_isDragging && ImGui::IsMouseDragging(0) && m_selectedPoint >= 0) {
        ImVec2 mouse_pos = ImGui::GetMousePos();
        m_tfPoints[m_selectedPoint].position = (mouse_pos.x - canvas_pos.x) / canvas_size.x;
        m_tfPoints[m_selectedPoint].position = std::clamp(m_tfPoints[m_selectedPoint].position, 0.0f, 1.0f);
        m_tfPoints[m_selectedPoint].alpha = 1.0f - (mouse_pos.y - canvas_pos.y) / canvas_size.y;
        m_tfPoints[m_selectedPoint].alpha = std::clamp(m_tfPoints[m_selectedPoint].alpha, 0.0f, 1.0f);
        updateTransferFunctionTexture();
    }
    
    if (ImGui::IsMouseReleased(0)) {
        m_isDragging = false;
        SortTFPoints();
    }
    
    // Draw control points
    for (size_t i = 0; i < m_tfPoints.size(); ++i) {
        float x = canvas_pos.x + m_tfPoints[i].position * canvas_size.x;
        float y = canvas_pos.y + (1.0f - m_tfPoints[i].alpha) * canvas_size.y;
        
        ImU32 color = ImColor(m_tfPoints[i].red, m_tfPoints[i].green, m_tfPoints[i].blue);
        ImU32 border_color = (i == m_selectedPoint) ? IM_COL32(255, 223, 0, 255) : IM_COL32(255, 255, 255, 200);
        
        // Draw guide line
        draw_list->AddLine(ImVec2(x, y), ImVec2(x, canvas_pos.y + canvas_size.y), IM_COL32(255, 255, 255, 60), 1.0f);
        
        // Draw point
        draw_list->AddCircleFilled(ImVec2(x, y), 6.0f, color);
        draw_list->AddCircle(ImVec2(x, y), 6.0f, border_color, 0, 2.0f);
    }
}

void Renderer::DrawChannelControls() {
    ImGui::Text("Channel Visualization");
    
    // Individual channel toggles
    static bool show_red = true, show_green = true, show_blue = true, show_alpha = true;
    
    ImGui::Checkbox("Red Channel", &show_red);
    ImGui::SameLine();
    ImGui::Checkbox("Green Channel", &show_green);
    ImGui::SameLine();
    ImGui::Checkbox("Blue Channel", &show_blue);
    ImGui::SameLine();
    ImGui::Checkbox("Opacity", &show_alpha);
    
    ImGui::Separator();
    
    // Instructions
    ImGui::TextWrapped("How to use:");
    ImGui::BulletText("Double-click to add control points");
    ImGui::BulletText("Drag points to adjust position/opacity");
    ImGui::BulletText("Right-click points to delete them");
    ImGui::BulletText("Use presets for quick setups");
    
    ImGui::Separator();
    
    // Quick actions
    if (ImGui::Button("Add Point", ImVec2(-1, 0))) {
        AddTFPoint(0.5f);
    }
    
    if (m_selectedPoint >= 0 && ImGui::Button("Delete Selected", ImVec2(-1, 0))) {
        DeleteTFPoint(m_selectedPoint);
    }
    
    if (ImGui::Button("Clear All", ImVec2(-1, 0))) {
        m_tfPoints.clear();
        m_tfPoints.push_back(TFPoint(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
        m_tfPoints.push_back(TFPoint(1.0f, 1.0f, 1.0f, 1.0f, 1.0f));
        updateTransferFunctionTexture();
    }
}

void Renderer::DrawPointProperties() {
    ImGui::Text("Point Properties");
    
    if (m_selectedPoint >= 0 && m_selectedPoint < m_tfPoints.size()) {
        TFPoint& point = m_tfPoints[m_selectedPoint];
        
        ImGui::PushItemWidth(-1);
        
        if (ImGui::SliderFloat("Position", &point.position, 0.0f, 1.0f, "%.3f")) {
            SortTFPoints();
            updateTransferFunctionTexture();
        }
        
        ImGui::Separator();
        
        if (ImGui::ColorEdit3("Color", &point.red, ImGuiColorEditFlags_Float)) {
            updateTransferFunctionTexture();
        }
        
        if (ImGui::SliderFloat("Opacity", &point.alpha, 0.0f, 1.0f, "%.2f")) {
            updateTransferFunctionTexture();
        }
        
        ImGui::PopItemWidth();
        
        // Color preview
        ImVec2 size(ImGui::GetContentRegionAvail().x, 30);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), 
                                ImColor(point.red, point.green, point.blue, point.alpha));
        draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 128));
        ImGui::Dummy(size);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No point selected");
        ImGui::Text("Click on a control point to edit its properties");
    }
}

void Renderer::DrawPresetsPanel() {
    ImGui::Text("Presets");
    
    if (ImGui::Button("Grayscale", ImVec2(-1, 0))) {
        m_tfPoints.clear();
        m_tfPoints.push_back(TFPoint(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
        m_tfPoints.push_back(TFPoint(1.0f, 1.0f, 1.0f, 1.0f, 1.0f));
        updateTransferFunctionTexture();
    }
    
    if (ImGui::Button("Rainbow", ImVec2(-1, 0))) {
        m_tfPoints.clear();
        m_tfPoints.push_back(TFPoint(0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
        m_tfPoints.push_back(TFPoint(0.25f, 0.0f, 1.0f, 1.0f, 0.3f));
        m_tfPoints.push_back(TFPoint(0.5f, 0.0f, 1.0f, 0.0f, 0.6f));
        m_tfPoints.push_back(TFPoint(0.75f, 1.0f, 1.0f, 0.0f, 0.8f));
        m_tfPoints.push_back(TFPoint(1.0f, 1.0f, 0.0f, 0.0f, 1.0f));
        updateTransferFunctionTexture();
    }
    
    if (ImGui::Button("Hot Metal", ImVec2(-1, 0))) {
        m_tfPoints.clear();
        m_tfPoints.push_back(TFPoint(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
        m_tfPoints.push_back(TFPoint(0.3f, 1.0f, 0.0f, 0.0f, 0.3f));
        m_tfPoints.push_back(TFPoint(0.6f, 1.0f, 1.0f, 0.0f, 0.7f));
        m_tfPoints.push_back(TFPoint(1.0f, 1.0f, 1.0f, 1.0f, 1.0f));
        updateTransferFunctionTexture();
    }
    
    if (ImGui::Button("CT Scan", ImVec2(-1, 0))) {
        m_tfPoints.clear();
        m_tfPoints.push_back(TFPoint(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
        m_tfPoints.push_back(TFPoint(0.2f, 0.0f, 0.0f, 0.5f, 0.1f));
        m_tfPoints.push_back(TFPoint(0.4f, 0.0f, 0.5f, 0.5f, 0.3f));
        m_tfPoints.push_back(TFPoint(0.6f, 0.5f, 0.5f, 0.0f, 0.6f));
        m_tfPoints.push_back(TFPoint(0.8f, 1.0f, 0.5f, 0.0f, 0.8f));
        m_tfPoints.push_back(TFPoint(1.0f, 1.0f, 1.0f, 1.0f, 1.0f));
        updateTransferFunctionTexture();
    }
    
    ImGui::Separator();
    ImGui::Text("Export/Import");
    
    if (ImGui::Button("Save TF", ImVec2(-1, 0))) {
        // Implement save functionality
    }
    
    if (ImGui::Button("Load TF", ImVec2(-1, 0))) {
        // Implement load functionality
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
