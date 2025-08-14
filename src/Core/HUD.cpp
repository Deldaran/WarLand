#include "HUD.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "../Platform/Input.h"
#include "../Engine/Rendering/World/FarMapRenderer.h"
#include "../Engine/Rendering/GL/TileMap.h"

void MapHUD::handleInput(Input* input) {
    if (!input) return;
    if (input->wasPressed(GLFW_KEY_L)) hudVisible_ = !hudVisible_;
}

void MapHUD::draw(FarMapRenderer* farMapRenderer, TileMap& worldMap) {
    if (!hudVisible_) return;
    ImGui::SetNextWindowPos(ImVec2(10,10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("HUD_Affichage", nullptr, flags)) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,2));
        ImGui::TextUnformatted("Affichage"); ImGui::SameLine();
        if (ImGui::SmallButton(hudCompact_?"+":"-")) hudCompact_ = !hudCompact_; ImGui::SameLine();
        if (ImGui::SmallButton("X")) hudVisible_=false;
        if (!hudCompact_) {
            bool sc = farMapRenderer ? farMapRenderer->showCountries() : false;
            if (ImGui::Checkbox("Pays", &sc)) { if (farMapRenderer) farMapRenderer->setShowCountries(sc); }
            ImGui::SameLine(); ImGui::Checkbox("Noms P", &showCountryNames_);
            ImGui::SameLine(); ImGui::Checkbox("Noms V", &showCityNames_);
            bool sa = farMapRenderer ? farMapRenderer->showAdaptive() : false;
            if (ImGui::Checkbox("Grille", &sa)) { if (farMapRenderer) farMapRenderer->setShowAdaptive(sa); }
            bool hs = farMapRenderer ? farMapRenderer->heightShading() : false; ImGui::SameLine();
            if (ImGui::Checkbox("Hauteur", &hs)) { if (farMapRenderer) farMapRenderer->setHeightShading(hs); }
            if (farMapRenderer) {
                float a = farMapRenderer->countryAlpha();
                ImGui::SliderFloat("Opacité Pays", &a, 0.f,1.f,"%.2f");
                if (a!=farMapRenderer->countryAlpha()) farMapRenderer->setCountryAlpha(a);
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Légende");
            ImGui::Dummy(ImVec2(0,2));
            ImGui::ColorButton("Deep##leg", ImVec4(0x08/255.f,0x30/255.f,0x44/255.f,1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(14,14)); ImGui::SameLine(); ImGui::TextUnformatted("Eau profonde");
            ImGui::ColorButton("Shal##leg", ImVec4(0x1A/255.f,0x58/255.f,0x78/255.f,1.f), ImGuiColorEditFlags_NoTooltip, ImVec2(14,14)); ImGui::SameLine(); ImGui::TextUnformatted("Eau faible");
            int shown=0; for(size_t i=0;i<worldMap.biomeNames.size() && i<worldMap.biomeColorsRGB.size() && shown<4; ++i){
                uint32_t rgb=worldMap.biomeColorsRGB[i]; ImVec4 c(((rgb>>16)&0xFF)/255.f,((rgb>>8)&0xFF)/255.f,(rgb&0xFF)/255.f,1.f);
                ImGui::ColorButton(("b"+std::to_string(i)).c_str(), c, ImGuiColorEditFlags_NoTooltip, ImVec2(14,14)); ImGui::SameLine();
                ImGui::TextUnformatted(worldMap.biomeNames[i].empty()?("Biome"+std::to_string(i)).c_str():worldMap.biomeNames[i].c_str());
                shown++; if (shown>=4) break;
            }
            ImGui::Separator();
            ImGui::TextDisabled("L pour cacher HUD");
        } else {
            ImGui::SameLine(); ImGui::TextDisabled("(compact)");
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();
}
