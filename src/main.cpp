// WarLand - point d'entree
// Phase 1 : rendu planetaire (icosphere subdivisee + relief procedural,
// coloration des biomes, shader d'atmosphere) avec camera orbitale et HUD ImGui.

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "core/Window.h"
#include "renderer/Shader.h"
#include "renderer/Camera.h"
#include "renderer/planet/PlanetMesh.h"
#include "renderer/overlay/ProvinceMap.h"
#include "simulation/SimulationWorld.h"
#include "simulation/SimulationRunner.h"

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

#ifndef WARLAND_ASSETS_DIR
#define WARLAND_ASSETS_DIR "."
#endif

namespace {

// Accumulateur de scroll alimente par le callback GLFW.
double g_scrollDelta = 0.0;
void scrollCallback(GLFWwindow*, double /*xoffset*/, double yoffset) {
    g_scrollDelta += yoffset;
}

// Couches de visualisation activables (panneau Filtres).
struct LayerState {
    bool politique = true;
    bool economie = false;
    bool population = false;
    bool climat = false;
    bool religion = false;
    bool langue = false;
    bool conflits = false;
};

// Formatage compact des grands nombres (population, stocks).
std::string formatNumber(double v) {
    char buf[32];
    if (v >= 1.0e9)      std::snprintf(buf, sizeof(buf), "%.2f Md", v / 1.0e9);
    else if (v >= 1.0e6) std::snprintf(buf, sizeof(buf), "%.2f M", v / 1.0e6);
    else if (v >= 1.0e3) std::snprintf(buf, sizeof(buf), "%.1f k", v / 1.0e3);
    else                 std::snprintf(buf, sizeof(buf), "%.0f", v);
    return buf;
}

// Saison globale a partir du temps in-game.
const char* seasonName(double timeDays) {
    double s = std::fmod(timeDays, 365.0) / 365.0;
    if (s < 0.0) s += 1.0;
    if (s < 0.25) return "Printemps";
    if (s < 0.50) return "Ete";
    if (s < 0.75) return "Automne";
    return "Hiver";
}

// Libelle meteo d'apres la pluviometrie (0..1).
const char* weatherLabel(double rain) {
    if (rain < 0.30) return "sec";
    if (rain > 0.80) return "orageux";
    if (rain > 0.55) return "pluvieux";
    return "nuageux";
}

// Rampe de couleur pour la heatmap de population (bleu -> rouge).
glm::vec3 heatColor(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec3 c0(0.10f, 0.15f, 0.45f); // froid / peu peuple
    const glm::vec3 c1(0.10f, 0.55f, 0.55f);
    const glm::vec3 c2(0.85f, 0.80f, 0.20f);
    const glm::vec3 c3(0.85f, 0.20f, 0.15f); // chaud / tres peuple
    if (t < 0.33f) return glm::mix(c0, c1, t / 0.33f);
    if (t < 0.66f) return glm::mix(c1, c2, (t - 0.33f) / 0.33f);
    return glm::mix(c2, c3, (t - 0.66f) / 0.34f);
}

void drawUI(wl::SimulationRunner& runner, const wl::SimulationRunner::Snapshot& snap,
            LayerState& layers, const wl::Camera& cam,
            const wl::PlanetMesh& planet, const wl::ProvinceMap& provinces,
            int selectedProvince, int selectedCiv, double fps) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // --- Barre superieure : date, vitesse, stabilite, alertes ---
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));
    ImGui::Begin("##topbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Text("An %d", snap.year);
        ImGui::SameLine(0, 20);
        {
            // Ere globale = celle de la civilisation la plus avancee.
            int era = 0;
            for (double t : snap.civTech) era = std::max(era, wl::SimulationWorld::eraForTech(t));
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1), "%s",
                               wl::SimulationWorld::eraName(era));
        }
        ImGui::SameLine(0, 16);
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), "%s", seasonName(snap.timeDays));
        ImGui::SameLine(0, 30);
        if (ImGui::Button(runner.paused() ? "Play" : "Pause")) runner.setPaused(!runner.paused());
        ImGui::SameLine();
        for (int s : {1, 2, 5, 10}) {
            ImGui::SameLine();
            bool active = (runner.speed() == s);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            if (ImGui::Button((std::string("x") + std::to_string(s)).c_str())) runner.setSpeed(s);
            if (active) ImGui::PopStyleColor();
        }
        ImGui::SameLine(0, 40);
        double stab = snap.stability * 100.0;
        ImVec4 stabCol = stab > 75 ? ImVec4(0.4f, 0.9f, 0.4f, 1)
                       : stab > 50 ? ImVec4(0.9f, 0.8f, 0.3f, 1)
                                   : ImVec4(0.95f, 0.4f, 0.3f, 1);
        ImGui::Text("Stabilite:");
        ImGui::SameLine();
        ImGui::TextColored(stabCol, "%.0f%%", stab);
        ImGui::SameLine(0, 40);
        ImGui::Text("Population: %s", formatNumber(snap.totalPopulation).c_str());
        ImGui::SameLine(0, 40);
        const std::string savePath = std::string(WARLAND_ASSETS_DIR) + "/saves/quicksave.json";
        if (ImGui::Button("Sauver")) runner.requestSave(savePath);
        ImGui::SameLine();
        if (ImGui::Button("Charger")) runner.requestLoad(savePath);
        ImGui::SameLine(0, 40);
        ImGui::Text("%.0f FPS", fps);
    }
    ImGui::End();

    // --- Panneau gauche : filtres de visualisation ---
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Filtres");
    {
        ImGui::Checkbox("Politique", &layers.politique);
        ImGui::Checkbox("Economie", &layers.economie);
        ImGui::Checkbox("Population", &layers.population);
        ImGui::Checkbox("Climat", &layers.climat);
        ImGui::Checkbox("Religion", &layers.religion);
        ImGui::Checkbox("Langue", &layers.langue);
        ImGui::Checkbox("Conflits", &layers.conflits);
        ImGui::Separator();
        ImGui::TextDisabled("Couches rendues : Politique");
        ImGui::TextDisabled("et Population (heatmap).");
    }
    ImGui::End();

    // --- Panneau droit : contexte de la zone selectionnee ---
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 260,
                                   vp->WorkPos.y + 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Contexte");
    {
        if (selectedProvince >= 0) {
            ImGui::Text("Province #%d", selectedProvince);
            (void)selectedCiv;

            wl::SimulationWorld::ProvinceState st;
            if (selectedProvince < static_cast<int>(snap.provinces.size()))
                st = snap.provinces[selectedProvince];
            if (st.valid) {
                if (st.ocean) {
                    ImGui::TextDisabled("Ocean");
                } else if (st.civ < 0) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.75f, 1), "Territoire sauvage");
                } else {
                    glm::vec3 col = provinces.civColor(st.civ);
                    ImGui::Text("Proprietaire : civ %d", st.civ);
                    ImGui::SameLine();
                    ImGui::ColorButton("##civ", ImVec4(col.r, col.g, col.b, 1.0f),
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                        ImVec2(16, 16));
                    ImGui::Text("Controle : %.0f%%", st.control);
                }
                ImGui::Spacing();
                ImGui::Text("Biome : %s", wl::SimulationWorld::biomeName(st.biome));
                if (st.ocean) {
                    ImGui::TextDisabled("Province oceanique (inhabitee)");
                } else if (st.civ < 0) {
                    ImGui::TextDisabled("Terre inhabitee");
                } else {
                    ImGui::Text("Population : %s", formatNumber(st.population).c_str());
                    if (st.foodBalance >= 0.0)
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "Nourriture : surplus");
                    else
                        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1), "Nourriture : FAMINE");
                    ImGui::Text("Meteo : %.0f%% (%s)", st.rainfall * 100.0,
                                weatherLabel(st.rainfall));
                    if (st.afflicted) {
                        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1), "Choc : %s",
                            wl::SimulationWorld::eventName(st.affliction));
                    }
                    ImGui::Separator();
                    ImGui::Text("Stocks");
                    ImGui::BulletText("Nourriture : %s", formatNumber(st.food).c_str());
                    ImGui::BulletText("Materiaux  : %s", formatNumber(st.materials).c_str());
                    ImGui::BulletText("Energie    : %s", formatNumber(st.energy).c_str());
                }
            }
        } else {
            ImGui::TextDisabled("Clic gauche sur une province");
            ImGui::TextDisabled("pour la selectionner");
        }
        ImGui::Separator();
        ImGui::Text("Planete");
        ImGui::BulletText("%d triangles", planet.triangleCount());
        ImGui::BulletText("%d sommets", planet.vertexCount());
        ImGui::BulletText("%d provinces", provinces.provinceCount());
        ImGui::BulletText("%d civilisations", provinces.civCount());
        ImGui::Separator();
        glm::vec3 p = cam.position();
        ImGui::Text("Camera: %.2f, %.2f, %.2f", p.x, p.y, p.z);
        ImGui::Text("Altitude: %.3f", glm::length(p) - 1.0f);
        ImGui::TextWrapped("Drag souris = orbite | Molette = zoom");
    }
    ImGui::End();

    // --- Timeline en bas ---
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - 80),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 80), ImGuiCond_FirstUseEver);
    // --- Panneau Diplomatie ---
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 260,
                                   vp->WorkPos.y + 370), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Diplomatie");
    {
        int c = snap.civCount;
        if (c <= 0) {
            ImGui::TextDisabled("(en cours d'initialisation)");
        } else {
            ImGui::TextDisabled("Population par civilisation");
            for (int i = 0; i < c && i < static_cast<int>(snap.civPopulation.size()); ++i) {
                glm::vec3 col = provinces.civColor(i);
                ImGui::ColorButton(("##c" + std::to_string(i)).c_str(),
                    ImVec4(col.r, col.g, col.b, 1.0f),
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                    ImVec2(14, 14));
                ImGui::SameLine();
                int era = (i < static_cast<int>(snap.civTech.size()))
                    ? wl::SimulationWorld::eraForTech(snap.civTech[i]) : 0;
                int nprov = (i < static_cast<int>(snap.civProvinceCount.size()))
                    ? snap.civProvinceCount[i] : 0;
                ImGui::Text("Civ %d : %s hab. - %d prov. [%s]", i,
                    formatNumber(snap.civPopulation[i]).c_str(), nprov,
                    wl::SimulationWorld::eraName(era));
            }
            ImGui::Separator();
            ImGui::TextDisabled("Relations (vert=allie, rouge=guerre)");
            // Matrice d'opinions.
            for (int a = 0; a < c; ++a) {
                for (int b = 0; b < c; ++b) {
                    float o = (a == b) ? 100.0f
                            : snap.opinion[a * c + b];
                    ImVec4 cell = a == b ? ImVec4(0.25f, 0.25f, 0.30f, 1)
                        : o <= -60.0f ? ImVec4(0.85f, 0.20f, 0.18f, 1)
                        : o >= 60.0f  ? ImVec4(0.25f, 0.75f, 0.30f, 1)
                        : ImVec4(0.35f + o / 600.0f, 0.35f, 0.35f - o / 600.0f, 1);
                    ImGui::ColorButton(("##o" + std::to_string(a) + "_" + std::to_string(b)).c_str(),
                        cell, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                        ImVec2(20, 20));
                    if (b < c - 1) ImGui::SameLine();
                }
            }
        }
    }
    ImGui::End();

    ImGui::Begin("Timeline des evenements");
    {
        const auto& evs = snap.events;
        if (evs.empty()) {
            ImGui::TextDisabled("Aucun evenement - accelere le temps (x10) et observe...");
        } else {
            // Du plus recent au plus ancien.
            for (auto it = evs.rbegin(); it != evs.rend(); ++it) {
                ImVec4 col = it->severity == 0 ? ImVec4(0.5f, 0.9f, 0.5f, 1)
                           : it->severity == 1 ? ImVec4(0.9f, 0.8f, 0.3f, 1)
                                               : ImVec4(0.95f, 0.45f, 0.35f, 1);
                ImGui::TextColored(col, "An %d", it->year);
                ImGui::SameLine();
                ImGui::TextUnformatted(it->text.c_str());
            }
        }
    }
    ImGui::End();
}

} // namespace

int main() {
    try {
        wl::Window window(1600, 900, "WarLand - Phase 9");
        glfwSetScrollCallback(window.handle(), scrollCallback);

        // --- Setup ImGui ---
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window.handle(), true);
        ImGui_ImplOpenGL3_Init("#version 450");

        // --- Shaders ---
        const std::string assets = WARLAND_ASSETS_DIR;
        wl::Shader planetShader;
        wl::Shader atmoShader;
        wl::Shader overlayShader;
        wl::Shader borderShader;
        wl::Shader cloudShader;
        if (!planetShader.loadFromFiles(assets + "/shaders/planet.vert",
                                        assets + "/shaders/planet.frag") ||
            !atmoShader.loadFromFiles(assets + "/shaders/atmosphere.vert",
                                      assets + "/shaders/atmosphere.frag") ||
            !overlayShader.loadFromFiles(assets + "/shaders/overlay.vert",
                                         assets + "/shaders/overlay.frag") ||
            !borderShader.loadFromFiles(assets + "/shaders/border.vert",
                                        assets + "/shaders/border.frag") ||
            !cloudShader.loadFromFiles(assets + "/shaders/atmosphere.vert",
                                       assets + "/shaders/clouds.frag")) {
            std::cerr << "[WarLand] Echec du chargement des shaders\n";
            return 1;
        }

        // Parametres de la coquille nuageuse et de l'atmosphere (fine, realiste).
        const float kCloudInner = 1.045f;
        const float kCloudOuter = 1.100f;
        const float kCloudDrawRadius = 1.115f; // sphere de rendu (englobe la coquille)
        const float kAtmoRadius = 1.115f;      // sommet de l'atmosphere (fine)
        const float kAtmoDrawRadius = 1.13f;   // sphere de rendu de l'atmosphere

        // Texture du champ nuageux simule (cycle de l'eau), mise a jour chaque frame.
        GLuint cloudTex = 0;
        glGenTextures(1, &cloudTex);
        glBindTexture(GL_TEXTURE_2D, cloudTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);          // longitude
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);   // latitude
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        int cloudTexW = 0, cloudTexH = 0;
        double cloudUploadTimer = 1.0; // force le 1er upload

        // --- Geometrie : la planete + la coque atmospherique ---
        wl::PlanetMesh::Params planetParams;
        planetParams.subdivisions = 6;
        planetParams.seaLevel = 0.0f;
        planetParams.amplitude = 0.04f;
        planetParams.seed = 1337u;
        wl::PlanetMesh planet(planetParams);

        wl::PlanetMesh::Params atmoParams;
        atmoParams.subdivisions = 4;
        atmoParams.amplitude = 0.0f; // sphere lisse
        wl::PlanetMesh atmosphere(atmoParams);

        // Couche politique : decoupage en provinces / civilisations.
        wl::ProvinceMap::Params provParams;
        provParams.provinces = 900; // provinces fines -> pays de depart petits
        provParams.civs = 10;
        wl::ProvinceMap provinces(planet, provParams);

        // Simulation sur son propre thread (decouplee du rendu).
        wl::SimulationRunner runner;
        runner.start(provinces, planetParams.seaLevel);

        wl::Camera camera(window.aspectRatio());
        LayerState layers;

        // Gestion de la couche overlay (politique ou heatmap de population).
        enum class OverlayMode { None, Political, Population };
        OverlayMode overlayMode = OverlayMode::None;
        double heatTimer = 0.0;
        std::vector<glm::vec3> heatColors(provinces.provinceCount());
        std::vector<int> ownerBuf(provinces.provinceCount(), -2);

        double lastTime = glfwGetTime();
        double lastMouseX = 0, lastMouseY = 0;
        bool dragging = false;

        // Etat de selection (picking).
        int selectedProvince = -1;
        int selectedCiv = -1;
        bool wasLeftDown = false;
        bool clickCandidate = false;
        double pressX = 0, pressY = 0;

        while (!window.shouldClose()) {
            double now = glfwGetTime();
            double dt = now - lastTime;
            lastTime = now;
            double fps = dt > 0 ? 1.0 / dt : 0.0;

            window.pollEvents();
            camera.setAspect(window.aspectRatio());

            ImGuiIO& io = ImGui::GetIO();

            // --- Controle camera (ignore si la souris est sur l'UI) ---
            double mx, my;
            glfwGetCursorPos(window.handle(), &mx, &my);
            bool leftDown = glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

            // Distinction clic (selection) vs glisser (orbite).
            bool doPick = false;
            double pickX = 0, pickY = 0;
            if (leftDown && !wasLeftDown && !io.WantCaptureMouse) {
                clickCandidate = true;
                pressX = mx;
                pressY = my;
            }
            if (clickCandidate) {
                double moved = std::hypot(mx - pressX, my - pressY);
                if (moved > 4.0) clickCandidate = false; // c'est un glisser
            }
            if (!leftDown && wasLeftDown && clickCandidate) {
                doPick = true;
                pickX = mx;
                pickY = my;
                clickCandidate = false;
            }
            wasLeftDown = leftDown;

            if (leftDown && !io.WantCaptureMouse) {
                if (!dragging) { dragging = true; lastMouseX = mx; lastMouseY = my; }
                float dx = static_cast<float>(mx - lastMouseX);
                float dy = static_cast<float>(my - lastMouseY);
                camera.orbit(dx * 0.3f, -dy * 0.3f);
            } else {
                dragging = false;
            }
            lastMouseX = mx;
            lastMouseY = my;

            if (!io.WantCaptureMouse && g_scrollDelta != 0.0) {
                camera.zoom(static_cast<float>(g_scrollDelta));
            }
            g_scrollDelta = 0.0;

            // --- Lecture de l'etat simule (snapshot publie par le thread sim) ---
            wl::SimulationRunner::Snapshot snap = runner.snapshot();

            // --- Coloration de l'overlay selon la couche active ---
            // Politique = appartenance courante (dynamique) ; Population = heatmap.
            OverlayMode desired = layers.population ? OverlayMode::Population
                                : layers.politique  ? OverlayMode::Political
                                                    : OverlayMode::None;
            heatTimer += dt;
            bool refresh = (desired != overlayMode) || heatTimer >= 0.30;
            if (desired != OverlayMode::None && !snap.provinces.empty() && refresh) {
                int count = std::min(provinces.provinceCount(),
                                     static_cast<int>(snap.provinces.size()));
                if (desired == OverlayMode::Political) {
                    for (int p = 0; p < count; ++p) {
                        const auto& st = snap.provinces[p];
                        if (st.ocean) {
                            heatColors[p] = glm::vec3(0.05f, 0.09f, 0.18f);
                            ownerBuf[p] = -2;
                        } else if (st.civ < 0) {
                            heatColors[p] = glm::vec3(0.22f, 0.22f, 0.24f); // sauvage
                            ownerBuf[p] = -1;
                        } else {
                            heatColors[p] = provinces.civColor(st.civ);
                            ownerBuf[p] = st.civ;
                        }
                    }
                    provinces.setProvinceColors(heatColors);
                    provinces.rebuildBorders(ownerBuf);
                } else { // Population
                    double maxP = snap.maxProvincePopulation;
                    for (int p = 0; p < count; ++p) {
                        const auto& st = snap.provinces[p];
                        if (st.ocean) {
                            heatColors[p] = glm::vec3(0.04f, 0.07f, 0.14f);
                        } else {
                            float t = maxP > 0.0
                                ? static_cast<float>(std::sqrt(st.population / maxP)) : 0.0f;
                            heatColors[p] = heatColor(t);
                        }
                    }
                    provinces.setProvinceColors(heatColors);
                }
                heatTimer = 0.0;
            }
            overlayMode = desired;

            // --- Matrices et soleil ---
            glm::mat4 view = camera.viewMatrix();
            glm::mat4 proj = camera.projectionMatrix();
            glm::mat4 viewProj = proj * view;
            glm::vec3 camPos = camera.position();

            // Le soleil tourne lentement autour de la planete (cycle jour/nuit).
            float sunAngle = static_cast<float>(now) * 0.05f;
            glm::vec3 sunDir = glm::normalize(
                glm::vec3(std::cos(sunAngle), 0.25f, std::sin(sunAngle)));

            // Rotation propre lente de la planete (partagee avec les nuages).
            float planetSpin = static_cast<float>(now) * 0.03f;
            glm::mat4 planetModel = glm::rotate(glm::mat4(1.0f),
                planetSpin, glm::vec3(0.0f, 1.0f, 0.0f));

            // --- Picking : ray-sphere depuis le curseur ---
            if (doPick) {
                float ndcX = static_cast<float>(2.0 * pickX / window.width() - 1.0);
                float ndcY = static_cast<float>(1.0 - 2.0 * pickY / window.height());
                glm::mat4 invVP = glm::inverse(viewProj);
                glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                glm::vec3 pNear = glm::vec3(nearH) / nearH.w;
                glm::vec3 pFar = glm::vec3(farH) / farH.w;
                glm::vec3 rd = glm::normalize(pFar - pNear);

                // Intersection avec la sphere unite centree a l'origine.
                float b = glm::dot(camPos, rd);
                float c = glm::dot(camPos, camPos) - 1.0f;
                float disc = b * b - c;
                if (disc >= 0.0f) {
                    float t = -b - std::sqrt(disc);
                    if (t < 0.0f) t = -b + std::sqrt(disc);
                    if (t >= 0.0f) {
                        glm::vec3 hit = camPos + t * rd;
                        // Retour en espace modele (rotation inverse = transposee).
                        glm::vec3 modelDir =
                            glm::transpose(glm::mat3(planetModel)) * hit;
                        provinces.pick(modelDir, selectedProvince, selectedCiv);
                    }
                }
            }

            // --- Rendu ---
            glClearColor(0.01f, 0.01f, 0.03f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // 1. La planete (opaque, depth on)
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            planetShader.bind();
            planetShader.setMat4("uModel", planetModel);
            planetShader.setMat4("uViewProj", viewProj);
            planetShader.setVec3("uCameraPos", camPos);
            planetShader.setVec3("uSunDir", sunDir);
            planetShader.setFloat("uSeaLevel", planetParams.seaLevel);
            planet.draw();

            // 2. Overlay (politique ou heatmap population) si une couche est active
            if (overlayMode != OverlayMode::None) {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);        // seul l'hemisphere visible
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                overlayShader.bind();
                overlayShader.setMat4("uModel", planetModel);
                overlayShader.setMat4("uViewProj", viewProj);
                overlayShader.setVec3("uSunDir", sunDir);
                overlayShader.setFloat("uAlpha", 0.55f);
                overlayShader.setFloat("uSelected", static_cast<float>(selectedProvince));
                provinces.draw();
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                glDisable(GL_CULL_FACE);

                // Frontieres nettes entre civilisations (couche politique).
                if (overlayMode == OverlayMode::Political) {
                    glLineWidth(1.5f);
                    borderShader.bind();
                    borderShader.setMat4("uModel", planetModel);
                    borderShader.setMat4("uViewProj", viewProj);
                    borderShader.setVec3("uColor", glm::vec3(0.02f, 0.02f, 0.04f));
                    provinces.drawBorders();
                }
            }

            float camDist = glm::length(camPos);

            // 3. Atmosphere : fine couche realiste (liseré lumineux a l'horizon).
            //    Dessinee AVANT les nuages pour que ceux-ci restent visibles au limbe.
            {
                glm::mat4 atmoModel = glm::scale(glm::mat4(1.0f), glm::vec3(kAtmoDrawRadius));
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);   // additif (glow)
                atmoShader.bind();
                atmoShader.setMat4("uModel", atmoModel);
                atmoShader.setMat4("uViewProj", viewProj);
                atmoShader.setVec3("uCameraPos", camPos);
                atmoShader.setVec3("uSunDir", sunDir);
                atmoShader.setFloat("uPlanetRadius", 1.0f);
                atmoShader.setFloat("uAtmoRadius", kAtmoRadius);
                atmoShader.setFloat("uStrength", 2.2f);
                atmosphere.draw();
                glDisable(GL_BLEND);
                glDisable(GL_CULL_FACE);
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
            }

            // 4. Nuages volumetriques (raymarching) avec LOD + fondu au zoom.
            //    Par-dessus l'atmosphere -> visibles aussi sur les cotes / au limbe.
            float cloudFade = glm::smoothstep(1.25f, 2.2f, camDist); // 0 pres du sol
            if (cloudFade > 0.001f && !snap.cloud.empty()) {
                // Mise a jour de la texture nuageuse (throttlee ~5 Hz).
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, cloudTex);
                cloudUploadTimer += dt;
                if (snap.cloudW != cloudTexW || snap.cloudH != cloudTexH) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, snap.cloudW, snap.cloudH, 0,
                                 GL_RED, GL_FLOAT, snap.cloud.data());
                    cloudTexW = snap.cloudW; cloudTexH = snap.cloudH;
                    cloudUploadTimer = 0.0;
                } else if (cloudUploadTimer >= 0.2) {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, snap.cloudW, snap.cloudH,
                                    GL_RED, GL_FLOAT, snap.cloud.data());
                    cloudUploadTimer = 0.0;
                }
                int cloudSteps = camDist > 6.0f ? 18 : (camDist > 3.0f ? 32 : 48);
                glm::mat4 cloudModel = glm::scale(glm::mat4(1.0f), glm::vec3(kCloudDrawRadius));
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                cloudShader.bind();
                cloudShader.setMat4("uModel", cloudModel);
                cloudShader.setMat4("uViewProj", viewProj);
                cloudShader.setVec3("uCameraPos", camPos);
                cloudShader.setVec3("uSunDir", sunDir);
                cloudShader.setFloat("uTime",
                    static_cast<float>(std::fmod(snap.timeDays, 100000.0)));
                cloudShader.setFloat("uPlanetSpin", planetSpin);
                cloudShader.setInt("uSteps", cloudSteps);
                cloudShader.setFloat("uFade", cloudFade);
                cloudShader.setFloat("uPlanetRadius", 1.0f);
                cloudShader.setFloat("uCloudInner", kCloudInner);
                cloudShader.setFloat("uCloudOuter", kCloudOuter);
                cloudShader.setFloat("uDensity", 95.0f);
                cloudShader.setInt("uCloudTex", 0);
                atmosphere.draw();
                glDisable(GL_BLEND);
                glDisable(GL_CULL_FACE);
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_TRUE);
            }

            // --- Rendu UI ---
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            drawUI(runner, snap, layers, camera, planet, provinces,
                   selectedProvince, selectedCiv, fps);
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            window.swapBuffers();
        }

        runner.stop(); // arret propre du thread de simulation

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    } catch (const std::exception& e) {
        std::cerr << "[WarLand] Erreur fatale: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
