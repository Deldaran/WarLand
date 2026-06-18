// WarLand - point d'entree
// Phase 1 : rendu planetaire (icosphere subdivisee + relief procedural,
// coloration des biomes, shader d'atmosphere) avec camera orbitale et HUD ImGui.

#ifdef _WIN32
#define NOMINMAX            // ne pas redefinir min/max (clash avec std::min/max)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>        // timeBeginPeriod -> sleeps precis pour le limiteur FPS
#include <timeapi.h>
#endif

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
#include <thread>
#include <chrono>

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

// Couleur d'une culture/langue (palette distincte des couleurs de civ).
glm::vec3 cultureColor(int c) {
    if (c < 0) return glm::vec3(0.25f);
    float h = std::fmod(0.05f + static_cast<float>(c) * 0.16f, 1.0f);
    float s = 0.5f, v = 0.9f;
    float i = std::floor(h * 6.0f), f = h * 6.0f - i;
    float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: return {v, t, p}; case 1: return {q, v, p}; case 2: return {p, v, t};
        case 3: return {p, q, v}; case 4: return {t, p, v}; default: return {v, p, q};
    }
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
            int& activePlanet, int planetCount, bool isSystem, bool& toggleView,
            int& timeUnit, int& timeMult, int& fpsLimit,
            int selectedProvince, int selectedCiv, double fps) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // --- Barre superieure : date, vitesse, stabilite, alertes ---
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));
    ImGui::Begin("##topbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    {
        // Age du monde (il demarre a l'an -3000 -> 0 an au depart).
        double ageDays = snap.timeDays + 3000.0 * 365.0;
        int ageYears = static_cast<int>(ageDays / 365.0);
        int ageDay = static_cast<int>(ageDays - static_cast<double>(ageYears) * 365.0);
        ImGui::Text("Age : %d ans (j%d)", ageYears, ageDay);
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
        // Echelles de temps : 1 seconde reelle = `mult` x unite. Le curseur regle
        // le multiplicateur (ex. 1 a 5 heures/s, 1 a 30 jours/s...).
        struct TimeScale { const char* name; double unit; int maxMult; };
        static const TimeScale scales[] = {
            {"Reel",  1.0 / 86400.0, 60},
            {"Heure", 1.0 / 24.0,    24},
            {"Jour",  1.0,           30},
            {"Mois",  30.0,          12},
            {"Annee", 365.0,         10},
        };
        const int nScales = 5;
        timeUnit = std::clamp(timeUnit, 0, nScales - 1);
        for (int i = 0; i < nScales; ++i) {
            ImGui::SameLine();
            bool active = (i == timeUnit);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            if (ImGui::Button(scales[i].name)) { timeUnit = i; timeMult = 1; runner.setPaused(false); }
            if (active) ImGui::PopStyleColor();
        }
        timeMult = std::clamp(timeMult, 1, scales[timeUnit].maxMult);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        std::string fmt = std::string("%d ") + scales[timeUnit].name + "/s";
        ImGui::SliderInt("##timemult", &timeMult, 1, scales[timeUnit].maxMult, fmt.c_str());
        runner.setDaysPerSecond(scales[timeUnit].unit * timeMult);
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
        // Vue systeme / planete + selecteur de monde.
        if (ImGui::Button(isSystem ? "Vue planete" : "Vue systeme")) toggleView = true;
        ImGui::SameLine();
        ImGui::Text("Monde %d/%d", activePlanet + 1, planetCount);
        ImGui::SameLine();
        if (ImGui::Button("<##planet")) activePlanet = (activePlanet + planetCount - 1) % planetCount;
        ImGui::SameLine();
        if (ImGui::Button(">##planet")) activePlanet = (activePlanet + 1) % planetCount;
        ImGui::SameLine(0, 40);
        const std::string savePath = std::string(WARLAND_ASSETS_DIR) + "/saves/quicksave_P"
                                     + std::to_string(activePlanet) + ".json";
        if (ImGui::Button("Sauver")) runner.requestSave(activePlanet, savePath);
        ImGui::SameLine();
        if (ImGui::Button("Charger")) runner.requestLoad(activePlanet, savePath);
        ImGui::SameLine(0, 40);
        ImGui::Text("%.0f FPS", fps);
        // Limiteur de FPS (frame pacing).
        struct FpsOpt { const char* name; int val; };
        static const FpsOpt fopts[] = {{"VSync", -1}, {"60", 60}, {"120", 120},
                                       {"144", 144}, {"Illim", 0}};
        for (const auto& o : fopts) {
            ImGui::SameLine();
            bool active = (fpsLimit == o.val);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            if (ImGui::Button((std::string(o.name) + "##fps").c_str())) fpsLimit = o.val;
            if (active) ImGui::PopStyleColor();
        }
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
        ImGui::TextDisabled("Couches : Politique, Population,");
        ImGui::TextDisabled("Langue (cultures).");
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
                    if (st.culture != st.ownerCulture)
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1),
                            "Culture %d (minorite)", st.culture);
                    else
                        ImGui::Text("Culture %d", st.culture);
                }
                ImGui::Spacing();
                ImGui::Text("Biome : %s", wl::SimulationWorld::biomeName(st.biome));
                if (st.ocean) {
                    ImGui::TextDisabled("Province oceanique (inhabitee)");
                } else if (st.civ < 0) {
                    ImGui::TextDisabled("Terre inhabitee");
                } else {
                    const char* tier = st.urbanPop < 2000 ? "Village"
                                     : st.urbanPop < 10000 ? "Bourg"
                                     : st.urbanPop < 30000 ? "Ville" : "Metropole";
                    ImGui::Text("Ville : %s (%s)", tier,
                                wl::SimulationWorld::specName(st.specialization));
                    ImGui::Text("Population : %s", formatNumber(st.population).c_str());
                    ImGui::Text("dont urbaine : %s", formatNumber(st.urbanPop).c_str());
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
#ifdef _WIN32
        timeBeginPeriod(1); // resolution timer 1 ms -> sleeps precis (limiteur FPS)
#endif

        // Limiteur de FPS : -1 = VSync, 0 = illimite, >0 = plafond (frame pacing).
        int fpsLimit = -1; // VSync par defaut
        int appliedSwap = 1; // glfwSwapInterval applique (1 = vsync, defini par Window)

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
        wl::Shader cityShader;
        if (!planetShader.loadFromFiles(assets + "/shaders/planet.vert",
                                        assets + "/shaders/planet.frag") ||
            !atmoShader.loadFromFiles(assets + "/shaders/atmosphere.vert",
                                      assets + "/shaders/atmosphere.frag") ||
            !overlayShader.loadFromFiles(assets + "/shaders/overlay.vert",
                                         assets + "/shaders/overlay.frag") ||
            !borderShader.loadFromFiles(assets + "/shaders/border.vert",
                                        assets + "/shaders/border.frag") ||
            !cloudShader.loadFromFiles(assets + "/shaders/atmosphere.vert",
                                       assets + "/shaders/clouds.frag") ||
            !cityShader.loadFromFiles(assets + "/shaders/city.vert",
                                      assets + "/shaders/city.frag")) {
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

        wl::PlanetMesh::Params atmoParams;
        atmoParams.subdivisions = 4;
        atmoParams.amplitude = 0.0f; // sphere lisse
        wl::PlanetMesh atmosphere(atmoParams);

        // --- Multi-planetes : un systeme de plusieurs mondes simules en parallele ---
        const int kNumPlanets = 3;
        wl::PlanetMesh::Params planetParams;
        planetParams.subdivisions = 6;
        planetParams.seaLevel = 0.0f;
        planetParams.amplitude = 0.04f;
        wl::ProvinceMap::Params provParams;
        provParams.provinces = 900; // provinces fines -> pays de depart petits
        provParams.civs = 10;

        std::vector<std::unique_ptr<wl::PlanetMesh>> planetMeshes;
        std::vector<std::unique_ptr<wl::ProvinceMap>> planetProvinces;
        std::vector<const wl::ProvinceMap*> planetPtrs;
        for (int i = 0; i < kNumPlanets; ++i) {
            wl::PlanetMesh::Params pp = planetParams;
            pp.seed = 1337u + static_cast<uint32_t>(i) * 7919u; // monde different
            auto mesh = std::make_unique<wl::PlanetMesh>(pp);
            auto prov = std::make_unique<wl::ProvinceMap>(*mesh, provParams);
            planetPtrs.push_back(prov.get());
            planetMeshes.push_back(std::move(mesh));
            planetProvinces.push_back(std::move(prov));
        }
        int activePlanet = 0, prevPlanet = 0;
        int timeUnit = 3, timeMult = 1; // echelle de temps (defaut : Mois x1)

        // Vue : planete (zoom sur un monde) ou systeme (orbites des planetes).
        enum class ViewMode { Planet, System };
        ViewMode viewMode = ViewMode::Planet;
        std::vector<float> orbitRadius(kNumPlanets);
        std::vector<float> orbitSpeed(kNumPlanets);
        std::vector<float> orbitPhase(kNumPlanets);
        for (int i = 0; i < kNumPlanets; ++i) {
            orbitRadius[i] = 5.0f + i * 4.0f;
            orbitSpeed[i] = 0.06f / (1.0f + i * 0.5f); // les planetes externes plus lentes
            orbitPhase[i] = i * 2.1f;
        }
        // Cercle d'orbite (line loop dans le plan XZ, rayon 1).
        GLuint orbitVao = 0, orbitVbo = 0;
        {
            std::vector<float> circ;
            const int seg = 96;
            for (int k = 0; k < seg; ++k) {
                float a = 2.0f * 3.14159265f * k / seg;
                circ.insert(circ.end(), {std::cos(a), 0.0f, std::sin(a)});
            }
            glGenVertexArrays(1, &orbitVao); glGenBuffers(1, &orbitVbo);
            glBindVertexArray(orbitVao);
            glBindBuffer(GL_ARRAY_BUFFER, orbitVbo);
            glBufferData(GL_ARRAY_BUFFER, circ.size() * sizeof(float), circ.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glBindVertexArray(0);
        }

        // Simulation sur son propre thread (toutes les planetes en parallele).
        wl::SimulationRunner runner;
        runner.start(planetPtrs, planetParams.seaLevel);

        wl::Camera camera(window.aspectRatio());
        LayerState layers;

        // Gestion de la couche overlay (politique, population ou culture/langue).
        enum class OverlayMode { None, Political, Population, Culture };
        OverlayMode overlayMode = OverlayMode::None;
        double heatTimer = 0.0;
        std::vector<glm::vec3> heatColors(provParams.provinces);
        std::vector<int> ownerBuf(provParams.provinces, -2);

        // Villes (points) et routes (lignes) reconstruites depuis le snapshot.
        GLuint cityVao = 0, cityVbo = 0, roadVao = 0, roadVbo = 0;
        glGenVertexArrays(1, &cityVao); glGenBuffers(1, &cityVbo);
        glBindVertexArray(cityVao);
        glBindBuffer(GL_ARRAY_BUFFER, cityVbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(4 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glGenVertexArrays(1, &roadVao); glGenBuffers(1, &roadVbo);
        glBindVertexArray(roadVao);
        glBindBuffer(GL_ARRAY_BUFFER, roadVbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
        int cityCount = 0, roadVertCount = 0;
        double cityTimer = 1.0;

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
            // FPS lisse : moyenne glissante exponentielle + affichage rafraichi
            // ~3x/s -> compteur stable (pas de saut 170/120/175 a chaque frame).
            double instFps = dt > 0 ? 1.0 / dt : 0.0;
            static double fpsSmooth = 0.0, fpsShown = 0.0, fpsTimer = 0.0;
            fpsSmooth = (fpsSmooth <= 0.0) ? instFps : fpsSmooth * 0.94 + instFps * 0.06;
            fpsTimer += dt;
            if (fpsShown <= 0.0 || fpsTimer >= 0.33) { fpsShown = fpsSmooth; fpsTimer = 0.0; }
            double fps = fpsShown;

            // Applique VSync (cap=-1) ou desactive (cap >= 0 -> limiteur logiciel).
            int wantSwap = (fpsLimit == -1) ? 1 : 0;
            if (wantSwap != appliedSwap) { glfwSwapInterval(wantSwap); appliedSwap = wantSwap; }

            window.pollEvents();
            camera.setAspect(window.aspectRatio());

            // Planete active : tout le rendu/picking opere sur ces references.
            wl::PlanetMesh& planet = *planetMeshes[activePlanet];
            wl::ProvinceMap& provinces = *planetProvinces[activePlanet];

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

            // --- Changement de planete : reinitialise selection et overlay ---
            if (activePlanet != prevPlanet) {
                prevPlanet = activePlanet;
                selectedProvince = -1;
                overlayMode = OverlayMode::None; // force la recoloration
                heatTimer = 1.0;
                cityTimer = 1.0;
            }

            // --- Lecture de l'etat simule (snapshot de la planete active) ---
            wl::SimulationRunner::Snapshot snap = runner.snapshot(activePlanet);

            // --- Coloration de l'overlay selon la couche active ---
            // Politique = appartenance courante (dynamique) ; Population = heatmap.
            OverlayMode desired = layers.population ? OverlayMode::Population
                                : layers.langue     ? OverlayMode::Culture
                                : layers.politique  ? OverlayMode::Political
                                                    : OverlayMode::None;
            heatTimer += dt;
            bool refresh = (desired != overlayMode) || heatTimer >= 0.5;
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
                } else if (desired == OverlayMode::Culture) {
                    // Carte des cultures/langues : couleur par culture, frontieres
                    // culturelles (les minorites conquises ressortent).
                    for (int p = 0; p < count; ++p) {
                        const auto& st = snap.provinces[p];
                        if (st.ocean) {
                            heatColors[p] = glm::vec3(0.05f, 0.09f, 0.18f);
                            ownerBuf[p] = -2;
                        } else if (st.civ < 0) {
                            heatColors[p] = glm::vec3(0.20f, 0.20f, 0.22f); // sauvage
                            ownerBuf[p] = -1;
                        } else {
                            heatColors[p] = cultureColor(st.culture);
                            ownerBuf[p] = st.culture;
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

            // --- Reconstruction des villes (points) et routes (lignes) ---
            cityTimer += dt;
            if (!snap.provinces.empty() && cityTimer >= 0.5) {
                cityTimer = 0.0;
                int count = std::min(provinces.provinceCount(),
                                     static_cast<int>(snap.provinces.size()));
                std::vector<float> cv, rv;
                for (int p = 0; p < count; ++p) {
                    const auto& st = snap.provinces[p];
                    if (st.ocean || st.civ < 0) continue;
                    glm::vec3 dir = provinces.provinceDir(p);
                    glm::vec3 pos = dir * 1.05f;
                    double upop = st.urbanPop; // taille = population URBAINE (urbanisation)
                    float size = upop < 2000 ? 5.0f : upop < 10000 ? 8.0f
                               : upop < 30000 ? 12.0f : 17.0f;
                    glm::vec3 cc = glm::mix(glm::vec3(1.0f, 0.95f, 0.6f),
                                            provinces.civColor(st.civ), 0.4f);
                    cv.insert(cv.end(), {pos.x, pos.y, pos.z, size, cc.r, cc.g, cc.b});
                    // Routes vers les provinces voisines de la meme civ.
                    for (int q : provinces.neighbors(p)) {
                        if (q <= p || q >= count) continue;
                        const auto& sq = snap.provinces[q];
                        if (sq.ocean || sq.civ != st.civ) continue;
                        glm::vec3 a = dir * 1.04f;
                        glm::vec3 b = provinces.provinceDir(q) * 1.04f;
                        rv.insert(rv.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
                    }
                }
                cityCount = static_cast<int>(cv.size() / 7);
                roadVertCount = static_cast<int>(rv.size() / 3);
                glBindBuffer(GL_ARRAY_BUFFER, cityVbo);
                glBufferData(GL_ARRAY_BUFFER, cv.size() * sizeof(float),
                             cv.empty() ? nullptr : cv.data(), GL_DYNAMIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, roadVbo);
                glBufferData(GL_ARRAY_BUFFER, rv.size() * sizeof(float),
                             rv.empty() ? nullptr : rv.data(), GL_DYNAMIC_DRAW);
            }

            // --- Matrices et soleil ---
            glm::mat4 view = camera.viewMatrix();
            glm::mat4 proj = camera.projectionMatrix();
            glm::mat4 viewProj = proj * view;
            glm::vec3 camPos = camera.position();

            // Rotation propre de la planete + jour/nuit : TEMPS REEL, pour que les
            // nuages bougent toujours visiblement (meme en mode lent / temps reel).
            // L'EVOLUTION meteo (fronts, formation/dissipation des nuages) est, elle,
            // calee sur le temps in-game via le climat -> rapide en "Annee", quasi
            // figee en "Reel".
            float sunAngle = static_cast<float>(now) * 0.05f;
            glm::vec3 sunDir = glm::normalize(
                glm::vec3(std::cos(sunAngle), 0.25f, std::sin(sunAngle)));

            float planetSpin = static_cast<float>(now) * 0.03f;
            glm::mat4 planetModel = glm::rotate(glm::mat4(1.0f),
                planetSpin, glm::vec3(0.0f, 1.0f, 0.0f));

            // --- Position des planetes dans le systeme (orbites animees) ---
            std::vector<glm::vec3> orbitPos(kNumPlanets);
            for (int i = 0; i < kNumPlanets; ++i) {
                float a = static_cast<float>(now) * orbitSpeed[i] + orbitPhase[i];
                orbitPos[i] = glm::vec3(std::cos(a), 0.0f, std::sin(a)) * orbitRadius[i];
            }

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

                if (viewMode == ViewMode::Planet) {
                    // Selection d'une province sur la planete active (sphere a l'origine).
                    float b = glm::dot(camPos, rd);
                    float c = glm::dot(camPos, camPos) - 1.0f;
                    float disc = b * b - c;
                    if (disc >= 0.0f) {
                        float t = -b - std::sqrt(disc);
                        if (t < 0.0f) t = -b + std::sqrt(disc);
                        if (t >= 0.0f) {
                            glm::vec3 hit = camPos + t * rd;
                            glm::vec3 modelDir = glm::transpose(glm::mat3(planetModel)) * hit;
                            provinces.pick(modelDir, selectedProvince, selectedCiv);
                        }
                    }
                } else {
                    // Vue systeme : clic sur une planete -> on voyage vers elle.
                    int hitPlanet = -1; float bestT = 1e9f;
                    for (int i = 0; i < kNumPlanets; ++i) {
                        glm::vec3 oc = camPos - orbitPos[i];
                        float b = glm::dot(oc, rd);
                        float c = glm::dot(oc, oc) - 1.1f * 1.1f; // rayon de clic
                        float disc = b * b - c;
                        if (disc < 0.0f) continue;
                        float t = -b - std::sqrt(disc);
                        if (t < 0.0f) t = -b + std::sqrt(disc);
                        if (t >= 0.0f && t < bestT) { bestT = t; hitPlanet = i; }
                    }
                    if (hitPlanet >= 0) {
                        activePlanet = hitPlanet;
                        viewMode = ViewMode::Planet;
                        camera.setDistance(3.0f);
                    }
                }
            }

            // --- Rendu ---
            glClearColor(0.01f, 0.01f, 0.03f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

          if (viewMode == ViewMode::System) {
            // ===== VUE SYSTEME : etoile centrale + planetes en orbite =====
            glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            // Etoile (sphere brillante).
            borderShader.bind();
            borderShader.setMat4("uViewProj", viewProj);
            borderShader.setMat4("uModel", glm::scale(glm::mat4(1.0f), glm::vec3(1.7f)));
            borderShader.setVec3("uColor", glm::vec3(1.0f, 0.88f, 0.45f));
            atmosphere.draw();
            // Anneaux d'orbite.
            borderShader.setVec3("uColor", glm::vec3(0.25f, 0.28f, 0.38f));
            glBindVertexArray(orbitVao);
            for (int i = 0; i < kNumPlanets; ++i) {
                borderShader.setMat4("uModel",
                    glm::scale(glm::mat4(1.0f), glm::vec3(orbitRadius[i])));
                glDrawArrays(GL_LINE_LOOP, 0, 96);
            }
            glBindVertexArray(0);
            // Planetes (terrain, eclairees par l'etoile centrale).
            planetShader.bind();
            planetShader.setMat4("uViewProj", viewProj);
            planetShader.setVec3("uCameraPos", camPos);
            planetShader.setFloat("uSeaLevel", planetParams.seaLevel);
            for (int i = 0; i < kNumPlanets; ++i) {
                glm::mat4 m = glm::translate(glm::mat4(1.0f), orbitPos[i])
                            * glm::rotate(glm::mat4(1.0f), planetSpin, glm::vec3(0, 1, 0))
                            * glm::scale(glm::mat4(1.0f), glm::vec3(i == activePlanet ? 1.0f : 0.85f));
                planetShader.setMat4("uModel", m);
                planetShader.setVec3("uSunDir", glm::normalize(-orbitPos[i])); // lumiere de l'etoile
                planetMeshes[i]->draw();
            }
          } else {
            // ===== VUE PLANETE =====
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
                overlayShader.setInt("uProvColor", 1);
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

            // 2b. Routes (lignes entre villes voisines d'une meme civ) + villes (points).
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (roadVertCount > 0) {
                glLineWidth(1.0f);
                borderShader.bind();
                borderShader.setMat4("uModel", planetModel);
                borderShader.setMat4("uViewProj", viewProj);
                borderShader.setVec3("uColor", glm::vec3(0.45f, 0.32f, 0.16f)); // route
                glBindVertexArray(roadVao);
                glDrawArrays(GL_LINES, 0, roadVertCount);
                glBindVertexArray(0);
            }
            if (cityCount > 0) {
                glEnable(GL_PROGRAM_POINT_SIZE);
                cityShader.bind();
                cityShader.setMat4("uModel", planetModel);
                cityShader.setMat4("uViewProj", viewProj);
                glBindVertexArray(cityVao);
                glDrawArrays(GL_POINTS, 0, cityCount);
                glBindVertexArray(0);
                glDisable(GL_PROGRAM_POINT_SIZE);
            }
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);

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
                } else if (cloudUploadTimer >= 0.4) {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, snap.cloudW, snap.cloudH,
                                    GL_RED, GL_FLOAT, snap.cloud.data());
                    cloudUploadTimer = 0.0;
                }
                int cloudSteps = camDist > 6.0f ? 8 : (camDist > 3.0f ? 12 : 18);
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
                cloudShader.setFloat("uMorphTime",
                    static_cast<float>(std::fmod(now, 100000.0))); // temps reel -> nuages vivants
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
          } // fin vue planete

            // --- Rendu UI ---
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            bool toggleView = false;
            drawUI(runner, snap, layers, camera, planet, provinces,
                   activePlanet, kNumPlanets, viewMode == ViewMode::System, toggleView,
                   timeUnit, timeMult, fpsLimit, selectedProvince, selectedCiv, fps);
            if (toggleView) {
                if (viewMode == ViewMode::System) {
                    viewMode = ViewMode::Planet;
                    camera.setDistance(3.0f);
                } else {
                    viewMode = ViewMode::System;
                    camera.setDistance(28.0f);
                }
            }
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            window.swapBuffers();

            // --- Limiteur de FPS : frame pacing precis (sleep grossier + spin fin) ---
            if (fpsLimit > 0) {
                double target = 1.0 / fpsLimit;
                for (;;) {
                    double el = glfwGetTime() - now; // now = debut de frame
                    if (el >= target) break;
                    double rem = target - el;
                    if (rem > 0.0015)
                        std::this_thread::sleep_for(std::chrono::duration<double>(rem - 0.001));
                    // sinon : busy-wait pour la precision finale
                }
            }
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
