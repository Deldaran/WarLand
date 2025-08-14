#pragma once
#include <string>

class LoadingScreen {
public:
    void draw(int w, int h, const std::string& status, float progress);
};
