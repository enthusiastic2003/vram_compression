#include "camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm> // For std::clamp
#include "imgui.h"

Camera::Camera(float distance, float fov)
    : distance_(distance),
      yaw_(45.0f),
      pitch_(-30.0f),
      target_(0.0f, 0.0f, 0.0f),
      up_(0.0f, 1.0f, 0.0f),
      fov_(fov),
      nearPlane_(0.1f),
      farPlane_(100.0f),
      mousePressed_(false),
      lastMouseX_(0.0),
      lastMouseY_(0.0),
      sensitivity_(0.25f),
      zoomSpeed_(0.5f),
      minDistance_(1.0f),
      maxDistance_(20.0f),
      minPitch_(-89.0f),
      maxPitch_(89.0f),
      defaultDistance_(distance),
      defaultYaw_(45.0f),
      defaultPitch_(-30.0f)
{
    clampPitch();
    clampDistance();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(getPosition(), target_, up_);
}

glm::mat4 Camera::getProjectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fov_), aspect, nearPlane_, farPlane_);
}

glm::mat4 Camera::getViewProjectionMatrix(float aspect) const {
    return getProjectionMatrix(aspect) * getViewMatrix();
}

glm::mat4 Camera::getModelRotationMatrix() const {
    glm::mat4 rotation = glm::mat4(1.0f);
    rotation = glm::rotate(rotation, glm::radians(yaw_), glm::vec3(0.0f, 1.0f, 0.0f));
    rotation = glm::rotate(rotation, glm::radians(pitch_), glm::vec3(1.0f, 0.0f, 0.0f));
    return rotation;
}

void Camera::onMouseButton(int button, int action, double xpos, double ypos) {
    if (button == 0) { // Left mouse button
        if (action == 1) { // Press
            mousePressed_ = true;
            lastMouseX_ = xpos;
            lastMouseY_ = ypos;
        } else { // Release
            mousePressed_ = false;
        }
    }
}

void Camera::onMouseMove(double xpos, double ypos) {
    if (!mousePressed_) return;

    float deltaX = static_cast<float>(xpos - lastMouseX_);
    float deltaY = static_cast<float>(ypos - lastMouseY_);

    yaw_ += deltaX * sensitivity_;
    pitch_ -= deltaY * sensitivity_; // Inverted Y

    clampPitch();

    lastMouseX_ = xpos;
    lastMouseY_ = ypos;
}

void Camera::onScroll(double yoffset) {
    distance_ -= static_cast<float>(yoffset) * zoomSpeed_;
    clampDistance();
}

void Camera::onKeyboard(int key, int action, float deltaTime) {
    float zoomChange = 10.0f * zoomSpeed_ * deltaTime;
    if (key == 265 && (action == 1 || action == 2)) { // GLFW_KEY_UP
        distance_ -= zoomChange;
    }
    if (key == 264 && (action == 1 || action == 2)) { // GLFW_KEY_DOWN
        distance_ += zoomChange;
    }
    clampDistance();
}

glm::vec3 Camera::getPosition() const {
    float radPitch = glm::radians(pitch_);
    float radYaw = glm::radians(yaw_);
    
    glm::vec3 pos;
    pos.x = target_.x + distance_ * cos(radPitch) * sin(radYaw);
    pos.y = target_.y + distance_ * sin(radPitch);
    pos.z = target_.z + distance_ * cos(radPitch) * cos(radYaw);
    
    return pos;
}

glm::vec3 Camera::getDirection() const {
    return glm::normalize(target_ - getPosition());
}

void Camera::setDistance(float distance) { 
    distance_ = distance; 
    clampDistance(); 
}

void Camera::setFOV(float fov) { 
    fov_ = fov; 
}

void Camera::setTarget(const glm::vec3& target) { 
    target_ = target; 
}

void Camera::setSensitivity(float sensitivity) { 
    sensitivity_ = sensitivity; 
}

void Camera::setZoomSpeed(float speed) { 
    zoomSpeed_ = speed; 
}

void Camera::setDistanceLimits(float minDist, float maxDist) { 
    minDistance_ = minDist; 
    maxDistance_ = maxDist; 
    clampDistance(); 
}

void Camera::setPitchLimits(float minPitch, float maxPitch) { 
    minPitch_ = minPitch; 
    maxPitch_ = maxPitch; 
    clampPitch(); 
}

void Camera::reset() {
    distance_ = defaultDistance_;
    yaw_ = defaultYaw_;
    pitch_ = defaultPitch_;
    clampPitch();
    clampDistance();
}

void Camera::renderImGuiControls() {
    if (ImGui::CollapsingHeader("Camera Controls")) {
        ImGui::Text("Position: (%.2f, %.2f, %.2f)", getPosition().x, getPosition().y, getPosition().z);
        ImGui::Text("Target:   (%.2f, %.2f, %.2f)", target_.x, target_.y, target_.z);
        
        if (ImGui::SliderFloat("Yaw", &yaw_, -180.0f, 180.0f)) {}
        if (ImGui::SliderFloat("Pitch", &pitch_, minPitch_, maxPitch_)) {
            clampPitch();
        }
        if (ImGui::SliderFloat("Distance", &distance_, minDistance_, maxDistance_)) {
            clampDistance();
        }
        if (ImGui::SliderFloat("FOV", &fov_, 10.0f, 120.0f)) {}

        ImGui::Separator();
        ImGui::SliderFloat("Sensitivity", &sensitivity_, 0.05f, 1.0f);
        ImGui::SliderFloat("Zoom Speed", &zoomSpeed_, 0.05f, 2.0f);

        if (ImGui::Button("Reset Camera")) {
            reset();
        }
    }
}

void Camera::clampDistance() {
    distance_ = std::max(minDistance_, std::min(distance_, maxDistance_));
}

void Camera::clampPitch() {
    pitch_ = std::max(minPitch_, std::min(pitch_, maxPitch_));
}
