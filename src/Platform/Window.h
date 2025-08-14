#pragma once
#include <string>
struct GLFWwindow;

struct WindowDesc {
    int width = 1280;
    int height = 720;
    const char* title = "Warland";
    bool vsync = true;
};

class Window {
public:
    bool init(const WindowDesc& d);
    void poll();
    void swap();
    bool shouldClose() const;
    void shutdown();

    GLFWwindow* handle() const { return window_; }
    void framebufferSize(int& w, int& h) const;

private:
    GLFWwindow* window_ = nullptr;
};
