#pragma once
#include <cstdint>
#include <array>
struct GLFWwindow;

enum class KeyState : uint8_t { Up, Down, Pressed, Released };

class Input {
public:
    bool init(GLFWwindow* window);
    void beginFrame();
    void endFrame();

    bool isDown(int key) const;       // GLFW_KEY_*
    bool wasPressed(int key) const;
    bool wasReleased(int key) const;

    void mousePosition(double& x, double& y) const;
    void mouseDelta(double& dx, double& dy) const;
    double scrollDelta() const;

private:
    static void KeyCallback(GLFWwindow*, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow*, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow*, double x, double y);
    static void ScrollCallback(GLFWwindow*, double xoff, double yoff);
    static void CharCallback(GLFWwindow*, unsigned int c);

private:
    std::array<KeyState, 512> keys_{}; // enough for GLFW key range
    std::array<KeyState, 16> mouse_{}; // buttons
    double mouseX_ = 0.0, mouseY_ = 0.0;
    double lastMouseX_ = 0.0, lastMouseY_ = 0.0;
    double scrollY_ = 0.0;
    GLFWwindow* window_ = nullptr;
};
