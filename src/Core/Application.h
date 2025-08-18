// [2] TODO: Boucle de jeu: init Platform (Window/Input), init Renderer, init Systems, run loop (fixed update + render), shutdown
// [2] TODO: Gestion des GameStates: WorldMapController, CityController, CharacterController
// [2] TODO: Injection de services (EventBus, ResourceManager, Config, Time)
// [2] TODO: Transitions de niveaux (sauvegarde partielle, activer/désactiver systèmes)

#pragma once
#include <memory>
#include <string>

#include "../Platform/Window.h"
#include "../Platform/Input.h"
#include "../Engine/Simulation/Scheduler.h"
#include "../Engine/Rendering/GL/TileMap.h"
#include "../Engine/Rendering/World/FarMapRenderer.h"
#include "../Engine/Rendering/World/SimpleWorldMeshRenderer.h"
#include "HUD.h"

struct GLFWwindow;

class Application {
public:
    bool init();  // crée fenêtre, GL, ImGui, etc.
    void run();   // boucle principale fixed + render
    void shutdown();
    ~Application();

private:
    void fixedUpdate(double dt);
    void render(double dt);
    void renderWorld(double dt);

private:
    GLFWwindow* window_ = nullptr;
    double accumulator_ = 0.0;
    double lastTime_ = 0.0;
    double fixedStep_ = 1.0 / 60.0;

    // Wrappers
    std::unique_ptr<Window> appWindow_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<Scheduler> scheduler_;

    // Far map world data & renderer
    TileMap worldMap_;
    std::unique_ptr<FarMapRenderer> farMapRenderer_;
    std::unique_ptr<SimpleWorldMeshRenderer> worldMeshRenderer_; // L2 (zoom proche)
    MapHUD mapHUD_; // HUD manager

    bool vsync_ = true;
};
