#include "Window.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

bool Window::init(const WindowDesc& d) {
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(d.width, d.height, d.title, nullptr, nullptr);
    if (!window_) return false;
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(d.vsync ? 1 : 0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return false;

    return true;
}

void Window::poll() {
    glfwPollEvents();
}

void Window::swap() {
    glfwSwapBuffers(window_);
}

bool Window::shouldClose() const { return glfwWindowShouldClose(window_); }

void Window::shutdown() {
    if (window_) { glfwDestroyWindow(window_); window_ = nullptr; }
    glfwTerminate();
}

void Window::framebufferSize(int& w, int& h) const {
    glfwGetFramebufferSize(window_, &w, &h);
}
