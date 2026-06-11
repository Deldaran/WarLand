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

#include <iostream>
#include <string>
#include <cmath>

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

// Etat de simulation factice pour faire vivre l'UI en attendant la Phase 3.
struct SimClock {
    bool paused = false;
    int speed = 1;            // x1, x2, x5, x10
    double accumulated = 0.0; // jours in-game ecoules
    int year = -3000;         // age de pierre

    void update(double dt) {
        if (paused) return;
        accumulated += dt * speed * 10.0; // 10 jours / s a x1
        while (accumulated >= 365.0) {
            accumulated -= 365.0;
            ++year;
        }
    }
};

void drawUI(SimClock& clock, LayerState& layers, const wl::Camera& cam,
            const wl::PlanetMesh& planet, const wl::ProvinceMap& provinces, double fps) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // --- Barre superieure : date, vitesse, stabilite, alertes ---
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, 0));
    ImGui::Begin("##topbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Text("An %d", clock.year);
        ImGui::SameLine(0, 30);
        if (ImGui::Button(clock.paused ? "Play" : "Pause")) clock.paused = !clock.paused;
        ImGui::SameLine();
        for (int s : {1, 2, 5, 10}) {
            ImGui::SameLine();
            bool active = (clock.speed == s);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.3f, 1.0f));
            if (ImGui::Button((std::string("x") + std::to_string(s)).c_str())) clock.speed = s;
            if (active) ImGui::PopStyleColor();
        }
        ImGui::SameLine(0, 40);
        ImGui::Text("Stabilite globale: 72%%");
        ImGui::SameLine(0, 40);
        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "Alertes: 0");
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
        ImGui::TextDisabled("Seule la couche Politique");
        ImGui::TextDisabled("est rendue (Phase 2).");
    }
    ImGui::End();

    // --- Panneau droit : contexte de la zone selectionnee ---
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 260,
                                   vp->WorkPos.y + 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Contexte");
    {
        ImGui::TextDisabled("Aucune zone selectionnee");
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
    ImGui::Begin("Timeline des evenements");
    ImGui::TextDisabled("(Phase 2 - les evenements de simulation apparaitront ici)");
    ImGui::End();
}

} // namespace

int main() {
    try {
        wl::Window window(1600, 900, "WarLand - Phase 2");
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
        if (!planetShader.loadFromFiles(assets + "/shaders/planet.vert",
                                        assets + "/shaders/planet.frag") ||
            !atmoShader.loadFromFiles(assets + "/shaders/atmosphere.vert",
                                      assets + "/shaders/atmosphere.frag") ||
            !overlayShader.loadFromFiles(assets + "/shaders/overlay.vert",
                                         assets + "/shaders/overlay.frag")) {
            std::cerr << "[WarLand] Echec du chargement des shaders\n";
            return 1;
        }

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

        const float atmoScale = 1.18f;
        const glm::vec3 planetCenter(0.0f);

        // Couche politique : decoupage en provinces / civilisations.
        wl::ProvinceMap::Params provParams;
        provParams.provinces = 220;
        provParams.civs = 7;
        wl::ProvinceMap provinces(planet, provParams);

        wl::Camera camera(window.aspectRatio());
        SimClock clock;
        LayerState layers;

        double lastTime = glfwGetTime();
        double lastMouseX = 0, lastMouseY = 0;
        bool dragging = false;

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

            clock.update(dt);

            // --- Matrices et soleil ---
            glm::mat4 view = camera.viewMatrix();
            glm::mat4 proj = camera.projectionMatrix();
            glm::mat4 viewProj = proj * view;
            glm::vec3 camPos = camera.position();

            // Le soleil tourne lentement autour de la planete (cycle jour/nuit).
            float sunAngle = static_cast<float>(now) * 0.05f;
            glm::vec3 sunDir = glm::normalize(
                glm::vec3(std::cos(sunAngle), 0.25f, std::sin(sunAngle)));

            // Rotation propre lente de la planete.
            glm::mat4 planetModel = glm::rotate(glm::mat4(1.0f),
                static_cast<float>(now) * 0.03f, glm::vec3(0.0f, 1.0f, 0.0f));

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

            // 2. Overlay politique (provinces/civilisations) si la couche est active
            if (layers.politique) {
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
                provinces.draw();
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                glDisable(GL_CULL_FACE);
            }

            // 3. L'atmosphere (halo additif autour du limbe)
            glm::mat4 atmoModel = glm::scale(glm::mat4(1.0f), glm::vec3(atmoScale));
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);          // on rend la face interne lointaine -> halo
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);   // additif
            atmoShader.bind();
            atmoShader.setMat4("uModel", atmoModel);
            atmoShader.setMat4("uViewProj", viewProj);
            atmoShader.setVec3("uCameraPos", camPos);
            atmoShader.setVec3("uSunDir", sunDir);
            atmoShader.setVec3("uPlanetCenter", planetCenter);
            atmosphere.draw();
            // restauration de l'etat
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glDisable(GL_CULL_FACE);

            // --- Rendu UI ---
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            drawUI(clock, layers, camera, planet, provinces, fps);
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            window.swapBuffers();
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    } catch (const std::exception& e) {
        std::cerr << "[WarLand] Erreur fatale: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
