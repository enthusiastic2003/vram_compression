#include <shader.h>
#include <glm/gtc/type_ptr.hpp> // for glm::value_ptr

Shader::Shader() {}

Shader::Shader(const std::string& vertexPath,
               const std::string& fragmentPath,
               const std::string& geometryPath)
    : vertexPath_(vertexPath), fragmentPath_(fragmentPath), geometryPath_(geometryPath), ID(0) {}

std::string Shader::loadShaderSource(const std::string& path) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "ERROR: Could not open shader file: " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void Shader::checkCompileErrors(GLuint shader, const std::string& type) const {
    GLint success;
    GLchar infoLog[1024];

    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR: Shader compilation failed (" << type << ")\n"
                      << infoLog << std::endl;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR: Program linking failed\n" << infoLog << std::endl;
        }
    }
}

bool Shader::compileAndLink() {
    std::string vertexCode = loadShaderSource(vertexPath_);
    std::string fragmentCode = loadShaderSource(fragmentPath_);
    std::string geometryCode;
    if (!geometryPath_.empty())
        geometryCode = loadShaderSource(geometryPath_);

    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();

    GLuint vertex, fragment, geometry;

    // Vertex shader
    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, nullptr);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    // Fragment shader
    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, nullptr);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    // Geometry shader
    if (!geometryCode.empty()) {
        const char* gShaderCode = geometryCode.c_str();
        geometry = glCreateShader(GL_GEOMETRY_SHADER);
        glShaderSource(geometry, 1, &gShaderCode, nullptr);
        glCompileShader(geometry);
        checkCompileErrors(geometry, "GEOMETRY");
    }

    // Shader program
    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    if (!geometryCode.empty())
        glAttachShader(ID, geometry);

    glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    // Delete shaders after linking
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!geometryCode.empty())
        glDeleteShader(geometry);

    return true;
}

void Shader::use() const {
    glUseProgram(ID);
}

void Shader::setInt(const std::string& name, int value) const {
    // Finds the location of the uniform variable 'name' in the shader program
    // and sets its value.
    GLint location = glGetUniformLocation(ID, name.c_str());
    if (location == -1) {
        std::cerr << "Warning: Uniform '" << name << "' not found." << std::endl;
    }
    glUniform1i(location, value);
}

void Shader::setFloat(const std::string& name, float value) const {
    // Finds the location of the uniform variable 'name' in the shader program
    // and sets its value.
    GLint location = glGetUniformLocation(ID, name.c_str());
    if (location == -1) {
        std::cerr << "Warning: Uniform '" << name << "' not found." << std::endl;
    }
    glUniform1f(location, value);
}

void Shader::setMat4(const std::string& name, const glm::mat4& matrix) const {
    // Find the location of the uniform variable in the shader program
    GLint location = glGetUniformLocation(ID, name.c_str());
    if (location == -1) {
        std::cerr << "Warning: Uniform '" << name << "' not found." << std::endl;
    } else {
        // Send the 4x4 matrix to the shader
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
        // 1      -> number of matrices
        // GL_FALSE -> do not transpose (GLM is column-major)
    }
}

void Shader::setBool(const std::string& name, bool value) const {
    // Finds the location of the uniform variable 'name' in the shader program
    // and sets its value.
    GLint location = glGetUniformLocation(ID, name.c_str());
    if (location == -1) {
        std::cerr << "Warning: Uniform '" << name << "' not found." << std::endl;
    }
    glUniform1i(location, static_cast<int>(value));
}

void Shader::setVec3(const std::string& name, float x, float y, float z) const {
    // Finds the location of the uniform variable 'name' in the shader program
    // and sets its value.
    GLint location = glGetUniformLocation(ID, name.c_str());
    if (location == -1) {
        std::cerr << "Warning: Uniform '" << name << "' not found." << std::endl;
    }
    glUniform3f(location, x, y, z);
}