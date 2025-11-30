#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "vtk_loader.hpp"
#include "camera.h"
#include "shader.h"
#include <memory>
#include "metrics.h"

// ImGui forward declarations
struct ImDrawList;
struct ImVec2;

// CUDA Includes
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <nanovdb/util/cuda/CudaDeviceBuffer.h>
#include <openvdb/openvdb.h>

class Renderer {
public:
    Renderer(int width, int height, const char* title)
        : width_(width), height_(height), title_(title),
          window(nullptr),
          camera_(3.0f, 45.0f)  // distance=3, fov=45
    {}

    ~Renderer();

    bool initialize(std::shared_ptr<VoxelLoader>);
    void run();

private:
    
        
    // Core members
    GLFWwindow* window;
    int width_;
    int height_;
    const char* title_;
    std::shared_ptr<VoxelLoader> m_voxelLoader;
    Camera camera_;
    cudaTextureObject_t m_volumeTex = 0;
    cudaTextureObject_t m_tfTexture = 0;
    cudaArray_t m_tfArray = nullptr;    
    // Inside Renderer.h
    int m_roiMin = 40;     // Default start of soft tissue (example)
    int m_roiMax = 80;     // Default end of soft tissue (example)
    bool m_useROI = false; // Toggle for your extension

    // Shader for displaying CUDA output
    Shader m_shader;

    // CUDA-OpenGL Interop
    GLuint m_cudaOutputTex = 0;
    cudaGraphicsResource* m_cudaResource = nullptr;
    void* m_deviceHandle = nullptr;  // NanoVDB grid on device

    // Full-screen quad for displaying CUDA output
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;

    // Transfer function parameters
    glm::vec3 m_color1 = glm::vec3(0.1f, 0.2f, 1.0f);
    glm::vec3 m_color2 = glm::vec3(1.0f, 1.0f, 1.0f);
    float m_alpha1 = 0.01f;
    float m_alpha2 = 0.4f;
    float m_threshold = 0.1f;

    // EXTENSION: Evaluation Controls
    openvdb::FloatGrid::Ptr m_originalGrid; // Backup of the original dense data
    float m_compressionQuality = 0.2f;      // 0.0 to 1.0 (Higher = Less Compression)
    int m_selectedMetric = 1;               // 0=f1, 1=f2, 2=f3

    // Helper to handle CPU compression -> NanoVDB conversion -> GPU Upload
    void RecompressVolume();


    // Rendering methods
    void renderScene();
    void renderUI();
    void initCudaInterop(); 
    void initQuad();
    
    // Transfer function UI helpers
    void DrawGradientPreview();
    void DrawControlPointsCanvas();
    bool DrawPointControls();
    bool DrawPresetButtons();
    void DrawHistogram(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size);

    // Event handlers
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPosition(double xpos, double ypos);
    void handleScroll(double xoffset, double yoffset);
    void handleKey(int key, int scancode, int action, int mods);
    void handleFramebufferSizeChange(int width, int height);
    void UpdateTransferFunctionOnGPU();

    // GLFW callbacks
    static void glfw_error_callback(int error, const char* description);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
    static void defaultKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);

    // Quality metrics storage - SIMPLIFIED
    QualityMetrics::Metrics m_qualityMetrics;

    // Quality evaluation methods
    void evaluateCompressionQuality();
    void displayQualityMetrics();
};
