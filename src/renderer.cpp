#include <iostream>
#include "renderer.hpp"
// Define this before including GLFW
#include <GLFW/glfw3.h>
// ImGui includes
#include "metrics.h"
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

// [Renderer.cpp] Add this new function

void Renderer::RecompressVolume() {
    if (!m_originalGrid) return;

    // 1. Map UI Selection to Paper's Metrics [cite: 169-173]
    std::string metricCode;
    switch (m_selectedMetric) {
        case 0: metricCode = "f1"; break; // Closest point
        case 1: metricCode = "f2"; break; // Farthest point
        case 2: metricCode = "f3"; break; // Median point
        default: metricCode = "f2";
    }

    std::cout << "[Extension] Recompressing... Rate: " << m_compressionQuality 
              << " | Metric: " << metricCode << std::endl;

    // 2. Prepare Source Grid
    // We must copy the original because the compressor might prune/modify the grid in place
    openvdb::FloatGrid::Ptr gridToCompress = m_originalGrid->deepCopy();

    // 3. Execute Compression (Algorithm 1)
    // This runs on the CPU and might take 100-500ms depending on data size
    vdb_compressor compressor(gridToCompress, m_compressionQuality);
    auto resultGrid = compressor.compress(metricCode); 

    // 4. Convert to NanoVDB
    auto handle = nanovdb::tools::createNanoGrid<openvdb::FloatGrid, float, nanovdb::cuda::DeviceBuffer>(*resultGrid);

    // 5. Update GPU Memory
    // IMPORTANT: Free previous VDB to prevent VRAM leak
    if (m_deviceHandle != nullptr) {
        FreeVDB(m_deviceHandle); 
    }
    
    cudaStream_t stream;
    cudaStreamCreate(&stream);
    m_deviceHandle = AllocAndUploadVDB(handle.data(), handle.size(), stream);
    
    // Wait for upload to finish before we let the renderer continue
    cudaStreamSynchronize(stream);
    cudaStreamDestroy(stream);

    std::cout << "-> Done. Active Voxels: " << resultGrid->activeVoxelCount() << std::endl;
}

#include "metrics.h"  // ADD THIS LINE AT THE TOP

// ... existing code ...

void Renderer::evaluateCompressionQuality() {
    if (!m_originalGrid) {
        std::cerr << "Error: No original grid available for quality evaluation" << std::endl;
        return;
    }
    
    // Create a temporary compressor to get compressed grid
    vdb_compressor compressor(m_originalGrid, m_compressionQuality);
    
    // Get the current compressed grid using the selected metric
    std::string metric_name;
    switch (m_selectedMetric) {
        case 0: metric_name = "f1"; break;
        case 1: metric_name = "f2"; break;
        case 2: metric_name = "f3"; break;
        default: metric_name = "f3"; break;
    }
    
    auto compressed_grid = compressor.compress(metric_name);
    
    // Calculate quality metrics using the new class
    m_qualityMetrics = QualityMetrics::calculate_quality_metrics(m_originalGrid, compressed_grid);
}

void Renderer::displayQualityMetrics() {
    if (ImGui::CollapsingHeader("Quality Evaluation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        
        if (ImGui::Button("Calculate MSE/PSNR", ImVec2(-1, 0))) {
            evaluateCompressionQuality();
        }
        
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Calculate Mean Squared Error and Peak Signal-to-Noise Ratio between original and compressed volumes");
        }
        
        ImGui::Spacing();
        
        if (m_qualityMetrics.metrics_calculated) {
            ImGui::Separator();
            ImGui::Text("Quality Metrics Results:");
            ImGui::Text("MSE: %.6f", m_qualityMetrics.mse);
            ImGui::Text("PSNR: %.2f dB", m_qualityMetrics.psnr);
            ImGui::Text("Compared Voxels: %zu", m_qualityMetrics.compared_voxels);
            
            // Quality interpretation with color coding
            ImGui::Spacing();
            ImGui::Text("Quality Assessment:");
            if (m_qualityMetrics.psnr > 40.0f) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Excellent (PSNR > 40 dB)");
            } else if (m_qualityMetrics.psnr > 30.0f) {
                ImGui::TextColored(ImVec4(0.5, 1, 0, 1), "Good (PSNR 30-40 dB)");
            } else if (m_qualityMetrics.psnr > 20.0f) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Acceptable (PSNR 20-30 dB)");
            } else {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Poor (PSNR < 20 dB)");
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Click 'Calculate MSE/PSNR' to see quality metrics");
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("MSE (Mean Squared Error): Lower is better");
        ImGui::TextWrapped("PSNR (Peak Signal-to-Noise Ratio): Higher is better");
    }
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
    float compressionQuality = 0.2f; // User-defined quality parameter [0.0 - 1.0]

    // vdb_compressor compressor(m_grid, compressionQuality);
    // m_grid = compressor.compress("f2"); // Using f2 similarity metric

    // 1. SAVE THE ORIGINAL DATA
    // We need a deep copy so we can re-compress from fresh source data every time.
    m_originalGrid = m_grid->deepCopy();

    // 2. PERFORM INITIAL COMPRESSION
    // Instead of writing the compression code here, we call our new helper.
    // This ensures what we see on startup matches the default UI values.
    RecompressVolume();


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

    // m_deviceHandle = AllocAndUploadVDB(handle.data(), handle.size(), stream);
    // VerifyVDB(m_deviceHandle);

    // --- TRANSFER FUNCTION SETUP ---
    // 1. Allocate CUDA Array for 1D Texture
    cudaChannelFormatDesc channelDesc = cudaCreateChannelDesc<float4>();
    cudaMallocArray(&m_tfArray, &channelDesc, 256, 1); // 256 width, 1 height

    // 2. Create Texture Object
    cudaResourceDesc resDesc;
    memset(&resDesc, 0, sizeof(resDesc));
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = m_tfArray;

    cudaTextureDesc texDesc;
    memset(&texDesc, 0, sizeof(texDesc));
    texDesc.addressMode[0] = cudaAddressModeClamp; // Clamp to edge
    texDesc.filterMode = cudaFilterModeLinear;     // Linear interpolation (Smooth!)
    texDesc.readMode = cudaReadModeElementType;
    texDesc.normalizedCoords = 1;                  // Use 0.0 to 1.0 coords

    cudaCreateTextureObject(&m_tfTexture, &resDesc, &texDesc, NULL);

    // 3. Perform initial upload so screen isn't black
    UpdateTransferFunctionOnGPU();

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

// Add this helper function definition
void Renderer::UpdateTransferFunctionOnGPU() {
    const int TF_SIZE = 256;
    std::vector<float4> tfData(TF_SIZE);

    for (int i = 0; i < TF_SIZE; ++i) {
        float density = i / (float)(TF_SIZE - 1);

        // 1. Threshold Logic (The Cutoff)
        if (density < m_threshold) {
            tfData[i] = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
        } 
        else {
            // 2. Linear Interpolation Logic
            float t = density; // Simple linear mapping for now
            
            // Interpolate Color
            glm::vec3 c = m_color1 + t * (m_color2 - m_color1);
            
            // Interpolate Alpha
            float a = m_alpha1 + t * (m_alpha2 - m_alpha1);

            tfData[i] = make_float4(c.r, c.g, c.b, a);
        }
    }

    // 3. Upload to CUDA Array
    // Note: m_tfArray needs to be allocated in initialize() (see below)
    cudaMemcpyToArray(m_tfArray, 0, 0, tfData.data(), TF_SIZE * sizeof(float4), cudaMemcpyHostToDevice);
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
    // IMPORTANT: We added 'm_tfTexture' to this call
    LaunchRayMarch(
        surface, width_, height_, 
        m_deviceHandle,
        m_tfTexture,          // <--- NEW: Pass the Transfer Function Texture here
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
//prak
void Renderer::DrawGradientPreview() {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y = 50.0f;
    
    // Draw gradient background with professional styling
    draw_list->AddRectFilled(canvas_pos, 
                            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
                            IM_COL32(25, 25, 25, 255));
    
    // Create a smooth gradient from color1 to color2
    const int segments = 256;
    for (int i = 0; i < segments; ++i) {
        float t = i / float(segments - 1);
        
        // Interpolate color
        glm::vec3 color = m_color1 + t * (m_color2 - m_color1);
        // Interpolate alpha
        float alpha = m_alpha1 + t * (m_alpha2 - m_alpha1);
        
        float x1 = canvas_pos.x + t * canvas_size.x;
        float x2 = canvas_pos.x + (t + 1.0f/segments) * canvas_size.x;
        
        draw_list->AddRectFilled(
            ImVec2(x1, canvas_pos.y),
            ImVec2(x2, canvas_pos.y + canvas_size.y),
            ImColor(color.r, color.g, color.b, alpha)
        );
    }
    
    // Draw border with subtle styling
    draw_list->AddRect(canvas_pos, 
                      ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
                      IM_COL32(100, 100, 100, 255), 0.0f, 0, 1.5f);
    
    // Draw value markers
    for (int i = 0; i <= 4; ++i) {
        float x = canvas_pos.x + (i / 4.0f) * canvas_size.x;
        draw_list->AddLine(ImVec2(x, canvas_pos.y + canvas_size.y - 10), 
                          ImVec2(x, canvas_pos.y + canvas_size.y), 
                          IM_COL32(200, 200, 200, 150));
        char label[8];
        snprintf(label, sizeof(label), "%.1f", i / 4.0f);
        draw_list->AddText(ImVec2(x - 8, canvas_pos.y + canvas_size.y + 2), 
                          IM_COL32(200, 200, 200, 255), label);
    }
    
    ImGui::Dummy(ImVec2(canvas_size.x, canvas_size.y + 20));
}

void Renderer::DrawControlPointsCanvas() {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y = 120.0f;
    
    // Draw professional background
    draw_list->AddRectFilled(canvas_pos, 
                            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
                            IM_COL32(30, 30, 30, 255));
    
    // Draw grid with subtle styling
    for (int i = 0; i <= 10; ++i) {
        float x = canvas_pos.x + (i / 10.0f) * canvas_size.x;
        draw_list->AddLine(ImVec2(x, canvas_pos.y), ImVec2(x, canvas_pos.y + canvas_size.y),
                          IM_COL32(60, 60, 60, 100));
    }
    for (int i = 0; i <= 5; ++i) {
        float y = canvas_pos.y + (i / 5.0f) * canvas_size.y;
        draw_list->AddLine(ImVec2(canvas_pos.x, y), ImVec2(canvas_pos.x + canvas_size.x, y),
                          IM_COL32(60, 60, 60, 100));
    }
    
    // Draw opacity curve
    std::vector<ImVec2> curve_points;
    for (int i = 0; i <= 100; ++i) {
        float t = i / 100.0f;
        float alpha = m_alpha1 + t * (m_alpha2 - m_alpha1);
        float x = canvas_pos.x + t * canvas_size.x;
        float y = canvas_pos.y + (1.0f - alpha) * canvas_size.y;
        curve_points.push_back(ImVec2(x, y));
    }
    
    if (curve_points.size() >= 2) {
        draw_list->AddPolyline(curve_points.data(), curve_points.size(), 
                              IM_COL32(76, 175, 255, 220), false, 3.0f);
    }
    
    // Draw control points
    ImVec2 start_point(canvas_pos.x, canvas_pos.y + (1.0f - m_alpha1) * canvas_size.y);
    ImVec2 end_point(canvas_pos.x + canvas_size.x, canvas_pos.y + (1.0f - m_alpha2) * canvas_size.y);
    
    // Start control point
    draw_list->AddCircleFilled(start_point, 6.0f, ImColor(m_color1.r, m_color1.g, m_color1.b));
    draw_list->AddCircle(start_point, 6.0f, IM_COL32(255, 255, 255, 200), 0, 2.0f);
    
    // End control point
    draw_list->AddCircleFilled(end_point, 6.0f, ImColor(m_color2.r, m_color2.g, m_color2.b));
    draw_list->AddCircle(end_point, 6.0f, IM_COL32(255, 255, 255, 200), 0, 2.0f);
    
    // Draw border
    draw_list->AddRect(canvas_pos, 
                      ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), 
                      IM_COL32(100, 100, 100, 255));
    
    ImGui::Dummy(canvas_size);
}

// Change void to bool
bool Renderer::DrawPointControls() {
    bool changed = false;
    
    ImGui::PushItemWidth(-1);
    
    if (ImGui::BeginTable("tf_controls", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextColumn();
        ImGui::Text("Start Point");
        // Check if values change
        changed |= ImGui::ColorEdit3("##StartColor", &m_color1.x);
        changed |= ImGui::SliderFloat("##StartAlpha", &m_alpha1, 0.0f, 1.0f);
        
        ImGui::TableNextColumn();
        ImGui::Text("End Point");
        changed |= ImGui::ColorEdit3("##EndColor", &m_color2.x);
        changed |= ImGui::SliderFloat("##EndAlpha", &m_alpha2, 0.0f, 1.0f);
        
        ImGui::EndTable();
    }
    
    changed |= ImGui::SliderFloat("Density Threshold", &m_threshold, 0.0f, 1.0f);
    
    ImGui::PopItemWidth();
    return changed;
}

// Returns true if a preset was clicked
bool Renderer::DrawPresetButtons() {
    bool clicked = false;

    // Preset buttons in a grid
    if (ImGui::BeginTable("presets", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextColumn();
        if (ImGui::Button("Grayscale", ImVec2(-1, 0))) {
            m_color1 = glm::vec3(0.1f, 0.1f, 0.1f);
            m_color2 = glm::vec3(1.0f, 1.0f, 1.0f);
            m_alpha1 = 0.0f;
            m_alpha2 = 1.0f;
            clicked = true;
        }
        
        ImGui::TableNextColumn();
        if (ImGui::Button("Rainbow", ImVec2(-1, 0))) {
            m_color1 = glm::vec3(0.0f, 0.0f, 1.0f);
            m_color2 = glm::vec3(1.0f, 0.0f, 0.0f);
            m_alpha1 = 0.1f;
            m_alpha2 = 0.8f;
            clicked = true;
        }
        
        ImGui::TableNextColumn();
        if (ImGui::Button("Hot Metal", ImVec2(-1, 0))) {
            m_color1 = glm::vec3(0.0f, 0.0f, 0.0f);
            m_color2 = glm::vec3(1.0f, 1.0f, 0.0f);
            m_alpha1 = 0.0f;
            m_alpha2 = 0.9f;
            clicked = true;
        }
        
        ImGui::EndTable();
    }
    
    // Action buttons
    ImGui::Spacing();
    if (ImGui::Button("Reset to Default", ImVec2(-1, 0))) {
        m_color1 = glm::vec3(0.1f, 0.2f, 1.0f);
        m_color2 = glm::vec3(1.0f, 1.0f, 1.0f);
        m_alpha1 = 0.01f;
        m_alpha2 = 0.4f;
        m_threshold = 0.1f;
        clicked = true;
    }

    return clicked;
}

void Renderer::DrawHistogram(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size) {
    // Placeholder - you can implement this later when you have histogram data
    // This function is declared but won't be called until you add histogram functionality
}

void Renderer::renderUI() {
    // Start new ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Build Professional Transfer Function Editor
    ImGui::Begin("Transfer Function Editor", nullptr, ImGuiWindowFlags_NoCollapse);
    
    // Header with description
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Volume Color Mapping");
    ImGui::TextWrapped("Adjust how density values map to color and opacity for volume rendering.");
    ImGui::Separator();
    
    // Gradient Preview Section
    ImGui::Text("Gradient Preview");
    // This is purely visual, no interaction logic needed yet
    DrawGradientPreview();
    
    // Control Points Section
    ImGui::Text("Opacity Control Points");
    // This is currently visual only
    DrawControlPointsCanvas();
    
    // Add quality evaluation section
    displayQualityMetrics();
    
    // --- INTERACTION LOGIC ---
    bool tfChanged = false;

    // Color and Opacity Controls
    ImGui::Separator();
    ImGui::Text("Point Properties");
    // If the user drags a slider, we mark changed as true
    if (DrawPointControls()) {
        tfChanged = true;
    }
    
    // Presets Section
    ImGui::Separator();
    ImGui::Text("Presets & Tools");
    // If the user clicks a preset, we mark changed as true
    if (DrawPresetButtons()) {
        tfChanged = true;
    }

    // --- NEW SECTION: COMPRESSION EXTENSION ---
    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Algorithm Evaluation");

    // 1. Similarity Function Selector (MCQ Style)
    // The paper compares these three metrics (Eq 1, 2, 3)
    const char* items[] = { 
        "f1: Closest (Aggressive)", 
        "f2: Farthest (Preserves Detail)", 
        "f3: Median (Balanced)" 
    };
    
    // Combo returns true immediately upon selection change
    if (ImGui::Combo("Metric", &m_selectedMetric, items, IM_ARRAYSIZE(items))) {
        RecompressVolume(); 
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose the heuristic for brick selection [Eq. 1-3].");

    // 2. Fixed-Rate Slider
    // Returns true while dragging, but we DON'T want to recompress then (too slow).
    ImGui::SliderFloat("Quality Rate", &m_compressionQuality, 0.01f, 1.0f, "%.2f");

    // Only trigger heavy recompression when user releases the mouse button
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        RecompressVolume();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Target compression rate. 1.0 = Lossless, 0.1 = 10% Size.");

    ImGui::Separator();
    
    // --- GPU UPDATE ---
    // Only upload to the GPU if something actually changed this frame.
    // This prevents PCI-E bus congestion.
    if (tfChanged) {
        UpdateTransferFunctionOnGPU(); 
    }

    ImGui::End();

    // Render Camera Controls (if you have them)
    camera_.renderImGuiControls();

    // Render ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Handle multiple OS windows (Viewports)
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
