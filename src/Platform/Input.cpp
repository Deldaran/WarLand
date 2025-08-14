#include "Input.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <imgui_impl_glfw.h>

static Input* s_input = nullptr;

bool Input::init(GLFWwindow* window) {
    window_ = window;
    s_input = this;
    std::fill(keys_.begin(), keys_.end(), KeyState::Up);
    std::fill(mouse_.begin(), mouse_.end(), KeyState::Up);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetCharCallback(window, CharCallback);
    return true;
}

void Input::beginFrame() {
    for (auto& k : keys_) if (k == KeyState::Pressed) k = KeyState::Down; else if (k == KeyState::Released) k = KeyState::Up;
    for (auto& b : mouse_) if (b == KeyState::Pressed) b = KeyState::Down; else if (b == KeyState::Released) b = KeyState::Up;
    lastMouseX_ = mouseX_; lastMouseY_ = mouseY_;
    scrollY_ = 0.0;
}

void Input::endFrame() {
    // nothing yet
}

bool Input::isDown(int key) const { return keys_[key] == KeyState::Down || keys_[key] == KeyState::Pressed; }
bool Input::wasPressed(int key) const { return keys_[key] == KeyState::Pressed; }
bool Input::wasReleased(int key) const { return keys_[key] == KeyState::Released; }

void Input::mousePosition(double& x, double& y) const { x = mouseX_; y = mouseY_; }
void Input::mouseDelta(double& dx, double& dy) const { dx = mouseX_ - lastMouseX_; dy = mouseY_ - lastMouseY_; }

double Input::scrollDelta() const { return scrollY_; }

void Input::KeyCallback(GLFWwindow* wnd, int key, int scancode, int action, int mods) {
    ImGui_ImplGlfw_KeyCallback(wnd, key, scancode, action, mods);
    if (!s_input || key < 0 || key >= (int)s_input->keys_.size()) return;
    if (action == GLFW_PRESS) s_input->keys_[key] = KeyState::Pressed;
    else if (action == GLFW_RELEASE) s_input->keys_[key] = KeyState::Released;
}

void Input::MouseButtonCallback(GLFWwindow* wnd, int button, int action, int mods) {
    ImGui_ImplGlfw_MouseButtonCallback(wnd, button, action, mods);
    if (!s_input || button < 0 || button >= (int)s_input->mouse_.size()) return;
    if (action == GLFW_PRESS) s_input->mouse_[button] = KeyState::Pressed;
    else if (action == GLFW_RELEASE) s_input->mouse_[button] = KeyState::Released;
}

void Input::CursorPosCallback(GLFWwindow* wnd, double x, double y) {
    ImGui_ImplGlfw_CursorPosCallback(wnd, x, y);
    if (!s_input) return; s_input->mouseX_ = x; s_input->mouseY_ = y;
}

void Input::ScrollCallback(GLFWwindow* wnd, double xoff, double yoff) {
    ImGui_ImplGlfw_ScrollCallback(wnd, xoff, yoff);
    if (!s_input) return; s_input->scrollY_ += yoff;
}

void Input::CharCallback(GLFWwindow* wnd, unsigned int c) {
    ImGui_ImplGlfw_CharCallback(wnd, c);
}
