#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>

// Forward declaration for GLFW keys
struct GLFWwindow;

class Camera {
public:
    explicit Camera(float distance = 3.0f, float fov = 45.0f);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspect) const;
    glm::mat4 getViewProjectionMatrix(float aspect) const;
    glm::mat4 getModelRotationMatrix() const;

    void onMouseButton(int button, int action, double xpos, double ypos);
    void onMouseMove(double xpos, double ypos);
    void onScroll(double yoffset);
    void onKeyboard(int key, int action, float deltaTime);

    glm::vec3 getPosition() const;
    glm::vec3 getTarget() const;
    glm::vec3 getDirection() const;
    glm::vec3 getUp() const;

    void setDistance(float distance);
    void setFOV(float fov);
    void setTarget(const glm::vec3& target);
    void setSensitivity(float sensitivity);
    void setZoomSpeed(float speed);
    void setDistanceLimits(float minDist, float maxDist);
    void setPitchLimits(float minPitch, float maxPitch);
    void reset();

    void renderImGuiControls();

    float getYaw() const;
    float getPitch() const;
    float getDistance() const;
    float getFOV() const;

private:
    void clampDistance();
    void clampPitch();

    float distance_;
    float yaw_;
    float pitch_;
    
    glm::vec3 target_;
    glm::vec3 up_;
    
    float fov_;
    float nearPlane_;
    float farPlane_;
    
    bool mousePressed_;
    double lastMouseX_;
    double lastMouseY_;
    
    float sensitivity_;
    float zoomSpeed_;
    float minDistance_;
    float maxDistance_;
    float minPitch_;
    float maxPitch_;
    
    float defaultDistance_;
    float defaultYaw_;
    float defaultPitch_;
};

#endif // CAMERA_H