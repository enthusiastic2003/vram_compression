#pragma once
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp> // for glm::value_ptr

class Shader {
public:
    // Constructor takes paths to vertex, fragment, and optional geometry shader
    Shader(const std::string& vertexPath,
           const std::string& fragmentPath,
           const std::string& geometryPath = "");
    
    Shader(); // Default constructor

    // Compile and link shaders
    bool compileAndLink();

    // Use the shader program
    void use() const;

    // Get program ID
    GLuint getID() const { return ID; }
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setMat4(const std::string& name, const glm::mat4& matrix) const;
    void setBool(const std::string& name, bool value) const ; // Added setBool method
    void setVec3(const std::string& name, float x, float y, float z) const; // Added setVec3 method



private:
    std::string vertexPath_;
    std::string fragmentPath_;
    std::string geometryPath_;
    GLuint ID;

    // Utility function to read shader source from file
    std::string loadShaderSource(const std::string& path) const;

    // Utility to check compile/link errors
    void checkCompileErrors(GLuint shader, const std::string& type) const;
};