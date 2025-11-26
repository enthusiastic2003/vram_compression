#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "vtk_loader.hpp"
#include "camera.h"
#include "shader.h"
#include <memory>
#include <vector>

// ImGui forward declarations
struct ImDrawList;
struct ImVec2;

// CUDA Includes
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <nanovdb/util/cuda/CudaDeviceBuffer.h>

class Renderer {
public:
    Renderer(int width, int height, const char* title)
        : width_(width), height_(height), 
          window(nullptr),
          title_(title),
          camera_(3.0f, 45.0f)
    {}

    ~Renderer();

    bool initialize(std::shared_ptr<VoxelLoader>);
    void run();

private:
    // Core members
    int width_;
    int height_;
    GLFWwindow* window;
    const char* title_;
    std::shared_ptr<VoxelLoader> m_voxelLoader;
    Camera camera_;

    // Shader for displaying CUDA output
    Shader m_shader;

    // CUDA-OpenGL Interop
    GLuint m_cudaOutputTex = 0;
    cudaGraphicsResource* m_cudaResource = nullptr;
    void* m_deviceHandle = nullptr;

    // Full-screen quad for displaying CUDA output
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;

    // Transfer function data
    struct TFPoint {
        float position;
        float red, green, blue, alpha;
        
        TFPoint(float pos = 0.0f, float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 0.0f)
            : position(pos), red(r), green(g), blue(b), alpha(a) {}
        
        bool operator<(const TFPoint& other) const { 
            return position < other.position; 
        }
    };
    
    std::vector<TFPoint> m_tfPoints;
    int m_selectedPoint = -1;
    bool m_isDragging = false;
    std::vector<float> m_histogram;
    unsigned int m_tfTexture = 0;

    // Rendering methods
    void renderScene();
    void renderUI();
    void initCudaInterop(); 
    void initQuad();
    void initTransferFunction();
    void updateTransferFunctionTexture();

    // Transfer function UI helpers
    void DrawTransferFunctionEditor();
    void DrawHistogramAndGradient();
    void DrawControlPoints();
    void DrawChannelControls();
    void DrawPresetsPanel();
    void DrawPointProperties();
    
    // TF operations
    void AddTFPoint(float position);
    void DeleteTFPoint(int index);
    void SortTFPoints();
    glm::vec4 SampleTF(float value) const;
    void GenerateHistogram();

    // Event handlers
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPosition(double xpos, double ypos);
    void handleScroll(double xoffset, double yoffset);
    void handleKey(int key, int scancode, int action, int mods);
    void handleFramebufferSizeChange(int width, int height);

    // GLFW callbacks
    static void glfw_error_callback(int error, const char* description);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
    static void defaultKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
};
