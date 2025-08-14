#include "LoadingScreen.h"
#include <imgui.h>

void LoadingScreen::draw(int w, int h, const std::string& status, float progress) {
    ImGui::SetNextWindowPos(ImVec2((float)w/2.f,(float)h/2.f), ImGuiCond_Always, ImVec2(0.5f,0.5f));
    ImGui::Begin("Chargement", nullptr, ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove);
    ImGui::Text("Chargement du monde");
    ImGui::Separator();
    ImGui::TextUnformatted(status.c_str());
    ImGui::ProgressBar(progress, ImVec2(320,22));
    ImGui::End();
}
