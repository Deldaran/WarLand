#include "Application.h"

#include <cstdio>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "../Platform/Window.h"
#include "../Platform/Input.h"
#include "../Engine/Simulation/Scheduler.h"
#include "../Tools/AssetPacker/AzgaarImporter.h"

static void SetupImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 450");
    // Charger fonte fantasy 8-bit si disponible (placer un .ttf dans assets/fonts)
    std::filesystem::path fontPath = std::filesystem::path("assets/fonts")/"piston-black.regular.ttf"; // updated filename
    if (std::filesystem::exists(fontPath)) {
        ImFontConfig cfg; cfg.OversampleH=2; cfg.OversampleV=2; cfg.PixelSnapH=true; cfg.SizePixels=16.0f; // taille de base
        io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 16.0f, &cfg, io.Fonts->GetGlyphRangesDefault());
    }
}

static void ShutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

static std::string FindAssetFile(const std::string& rel) {
    static const char* roots[] = { "", "../", "../../" };
    for (auto r : roots) {
        std::filesystem::path p = std::filesystem::path(r) / rel;
        std::error_code ec; if (std::filesystem::exists(p, ec)) return p.string();
    }
    return rel; // fallback (will likely fail later)
}

Application::~Application() = default;

bool Application::init() {
    // Window & ImGui
    appWindow_ = std::make_unique<Window>(); WindowDesc desc{}; desc.width=1280; desc.height=720; desc.title="Warland"; desc.vsync=vsync_;
    if (!appWindow_->init(desc)) { std::fprintf(stderr, "Window init failed\n"); return false; }
    window_ = appWindow_->handle(); SetupImGui(window_);
    input_ = std::make_unique<Input>(); input_->init(window_);
    scheduler_ = std::make_unique<Scheduler>();

    // Inline loading with console logs
    const std::string relPath = "assets/maps/WarLand Full 2025-08-14-10-43.json";
    std::string fullPath = FindAssetFile(relPath);
    std::fprintf(stderr, "[Load] Fichier cible: %s (cwd=%s)\n", fullPath.c_str(), std::filesystem::current_path().string().c_str());

    AzgaarImportConfig cfg; cfg.targetWidth=2000; cfg.targetHeight=2000; cfg.keepAzgaarNames=true;
    AzgaarImportResult azRes;
    std::fprintf(stderr, "[Load] Import Azgaar...\n");
    if (!AzgaarImporter::Load(fullPath, cfg, "", 123456789ull, azRes)) {
        std::fprintf(stderr, "[Load][ERREUR] Echec import: %s\n", fullPath.c_str());
    } else {
        std::fprintf(stderr, "[Load] Import OK: cells=%d places=%zu countriesColors=%zu\n", azRes.sourceCellCount, azRes.map.places.size(), azRes.map.countryColorsRGB.size());
        worldMap_ = std::move(azRes.map);
        std::fprintf(stderr, "[Load] Map transférée (size=%dx%d)\n", worldMap_.width, worldMap_.height);
        farMapRenderer_ = std::make_unique<FarMapRenderer>();
        if (farMapRenderer_->init(&worldMap_)) {
            std::fprintf(stderr, "[Load] FarMapRenderer initialisé\n");
        } else {
            std::fprintf(stderr, "[Load][ERREUR] FarMapRenderer init failed\n");
        }
    }

    lastTime_=glfwGetTime(); accumulator_=0.0; fixedStep_=1.0/60.0; return true;
}

void Application::fixedUpdate(double dt) {
    if (scheduler_) scheduler_->updateFixed(dt);
}

void Application::render(double /*dt*/) {
    int w, h; appWindow_->framebufferSize(w, h); glViewport(0,0,w,h); glClearColor(0.08f,0.09f,0.11f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
    static float camX=0.f, camY=0.f; 
    static float baseZoom=1.f; // ajuste pour que la carte tienne dans la fenêtre (largeur)
    static float zoomFactor=1.f; // facteur utilisateur
    static int lastW=0, lastH=0; 
    const float mapWidthUnits = worldMap_.worldMaxX > 0 ? worldMap_.worldMaxX : (float)worldMap_.width;
    const float mapHeightUnits = worldMap_.worldMaxY > 0 ? worldMap_.worldMaxY : (float)worldMap_.height;
    // Recalcul si première frame ou resize
    if (lastW!=w || lastH!=h) {
        baseZoom = (mapWidthUnits>0)? (float)w / mapWidthUnits : 1.f;
        lastW=w; lastH=h;
        camX = 0.f; camY = 0.f;
    }
    // Input déplacement (utilise zoom total)
    float totalZoom = baseZoom * zoomFactor;
    if (input_->isDown(GLFW_KEY_W)) camY -= 600.f * (1/totalZoom) * (float)fixedStep_;
    if (input_->isDown(GLFW_KEY_S)) camY += 600.f * (1/totalZoom) * (float)fixedStep_;
    if (input_->isDown(GLFW_KEY_A)) camX -= 600.f * (1/totalZoom) * (float)fixedStep_;
    if (input_->isDown(GLFW_KEY_D)) camX += 600.f * (1/totalZoom) * (float)fixedStep_;

    // Scroll zoom ancré sur la position souris (point monde sous le curseur reste stable)
    double mx, my; input_->mousePosition(mx, my); // coords fenêtre en pixels
    double scroll = input_->scrollDelta();
    if (scroll != 0.0) {
        float oldTotalZoom = totalZoom;
        float factor = (scroll > 0 ? 1.1f : 0.9f);
        zoomFactor *= factor; // no clamp
        totalZoom = baseZoom * zoomFactor;
        if (totalZoom != oldTotalZoom) {
            float worldXBefore = camX + (float)mx / oldTotalZoom;
            float worldYBefore = camY + (float)my / oldTotalZoom;
            camX = worldXBefore - (float)mx / totalZoom;
            camY = worldYBefore - (float)my / totalZoom;
        }
    }
    // No clamping on camX camY or zoomFactor
    glm::mat4 proj = glm::ortho(0.0f,(float)w/totalZoom,(float)h/totalZoom,0.0f);
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-camX,-camY,0));
    glm::mat4 vp = proj*view;
    if (farMapRenderer_) farMapRenderer_->render(&worldMap_, vp, totalZoom);

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

    // Delegate HUD input + drawing to MapHUD
    mapHUD_.handleInput(input_.get());
    mapHUD_.draw(farMapRenderer_.get(), worldMap_);

    // Calque labels monde (avant fenêtre UI) : utiliser draw list background pour rester sous les fenêtres
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // Conversion monde->écran: sx = totalZoom, sy = totalZoom, offset = (-camX*totalZoom, -camY*totalZoom)
    auto worldToScreen = [&](float wx, float wy){ ImVec2 p; p.x = (wx - camX)*totalZoom; p.y = (wy - camY)*totalZoom; return p; };

    // Affichage noms des pays (via HUD toggle)
    if (mapHUD_.showCountryNames()) {
        for (auto &ci : worldMap_.countryInfos) {
            if (ci.id<=0) continue;
            if (farMapRenderer_ && farMapRenderer_->isCountryNeutral((uint16_t)ci.id)) continue;
            float baseSize = 22.0f; float scale = (worldMap_.countryInfos.size()>40)?0.8f:1.0f;
            ImVec2 pos = worldToScreen(ci.x, ci.y);
            if (pos.x < -200 || pos.y < -100 || pos.x > 5000 || pos.y > 4000) continue;
            ImGuiIO& io = ImGui::GetIO(); ImFont* font = io.Fonts->Fonts.empty()? nullptr : io.Fonts->Fonts[0];
            const char* txt = ci.name.empty()?"?":ci.name.c_str(); float sizePx = baseSize*scale;
            ImU32 colMain = IM_COL32(255,250,240,255); ImU32 colOutline = IM_COL32(0,0,0,255);
            ImVec2 textSize = font? font->CalcTextSizeA(sizePx, FLT_MAX, 0.f, txt) : ImVec2(0,0);
            ImVec2 anchor = ImVec2(pos.x - textSize.x*0.5f, pos.y - textSize.y*0.5f);
            ImGui::PushFont(font); static const ImVec2 offs[8] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
            for (auto &o : offs) dl->AddText(font, sizePx, ImVec2(anchor.x+o.x, anchor.y+o.y), colOutline, txt);
            dl->AddText(font, sizePx, anchor, colMain, txt); ImGui::PopFont();
        }
    }

    // Affichage noms des villes (via HUD toggle)
    if (mapHUD_.showCityNames() && totalZoom > 2.5f) {
        float sx = (worldMap_.width>1 && worldMap_.worldMaxX>0)? worldMap_.worldMaxX / (float)(worldMap_.width -1) : 1.f;
        float sy = (worldMap_.height>1 && worldMap_.worldMaxY>0)? worldMap_.worldMaxY / (float)(worldMap_.height-1) : 1.f;
        for (auto &pl : worldMap_.places) {
            if (pl.name.empty()) continue;
            float wx = pl.x * sx; float wy = pl.y * sy;
            ImVec2 pos = worldToScreen(wx, wy); if (pos.x < -50 || pos.y < -50 || pos.x > (float)w+50 || pos.y > (float)h+50) continue;
            float fontSize = 14.0f; ImGuiIO& io = ImGui::GetIO(); ImFont* font = io.Fonts->Fonts.empty()? nullptr : io.Fonts->Fonts[0];
            const char* txt = pl.name.c_str(); ImVec2 textSize = font? font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, txt) : ImVec2(0,0);
            float offsetWorld = 3.0f; ImVec2 posAbove = worldToScreen(wx, wy - offsetWorld);
            ImVec2 anchor = ImVec2(posAbove.x - textSize.x*0.5f, posAbove.y - textSize.y);
            ImU32 colMain = IM_COL32(255,255,255,255); ImU32 colOutline = IM_COL32(0,0,0,255);
            ImGui::PushFont(font); static const ImVec2 offsC[8] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
            for (auto &o : offsC) dl->AddText(font, fontSize, ImVec2(anchor.x+o.x, anchor.y+o.y), colOutline, txt);
            dl->AddText(font, fontSize, anchor, colMain, txt); ImGui::PopFont();
        }
    }

    ImGui::Begin("Warland");
    ImGui::Text("Map %dx%d", worldMap_.width, worldMap_.height);
    ImGui::Text("Countries palette: %zu", worldMap_.countryColorsRGB.size());
    ImGui::Text("Zoom L1: %.2f (cache >7.50)", totalZoom);
    ImGui::Text("Cam (%.0f, %.0f)", camX, camY);
    ImGui::Text("Places: %zu Roads: %zu", worldMap_.places.size(), worldMap_.roads.size());
    bool showAdaptive = farMapRenderer_ ? farMapRenderer_->showAdaptive() : false; if (ImGui::Checkbox("Grille adaptative (L1)", &showAdaptive)) { if (farMapRenderer_) farMapRenderer_->setShowAdaptive(showAdaptive); }
    bool heightShade = farMapRenderer_ ? farMapRenderer_->heightShading() : false; if (ImGui::Checkbox("Shading hauteur", &heightShade)) { if (farMapRenderer_) farMapRenderer_->setHeightShading(heightShade); }
    bool showCountries = farMapRenderer_ ? farMapRenderer_->showCountries() : true; if (ImGui::Checkbox("Overlay pays", &showCountries)) { if (farMapRenderer_) farMapRenderer_->setShowCountries(showCountries); }
    if (farMapRenderer_) {
        float alpha = farMapRenderer_->countryAlpha();
        if (ImGui::SliderFloat("Opacité pays", &alpha, 0.0f, 1.0f, "%.2f")) { farMapRenderer_->setCountryAlpha(alpha); }
        static int neutralId = 1; // défaut id 1 neutre
        ImGui::InputInt("Country neutre ID", &neutralId);
        if (ImGui::Button("Toggle neutre (ID actuel)") && neutralId>=0 && neutralId<256) {
            bool cur = farMapRenderer_->isCountryNeutral((uint16_t)neutralId);
            farMapRenderer_->setCountryNeutral((uint16_t)neutralId, !cur);
        }
    }
    // Echelle km
    const float worldKmWidth = 2700.f; // largeur réelle
    float kmPerUnit = worldKmWidth / mapWidthUnits;
    float viewWorldWidth = (float)w / totalZoom;
    float viewKmWidth = viewWorldWidth * kmPerUnit;
    ImGui::Text("Zoom total: %.3f (base=%.3f factor=%.3f)", totalZoom, baseZoom, zoomFactor);
    ImGui::Text("Largeur vue: %.0f u (%.0f km)", viewWorldWidth, viewKmWidth);
    // Debug biome sous curseur
    if (worldMap_.width>0 && worldMap_.height>0) {
        // Position souris -> coord monde -> coord grille (discrète)
        float worldMouseX = camX + (float)mx / totalZoom;
        float worldMouseY = camY + (float)my / totalZoom;
        int gx = (int)std::floor(worldMouseX / worldMap_.worldMaxX * (worldMap_.width  - 1));
        int gy = (int)std::floor(worldMouseY / worldMap_.worldMaxY * (worldMap_.height - 1));
        if (worldMap_.worldMaxX <= 0 || worldMap_.worldMaxY <= 0) { gx = (int)std::floor(worldMouseX); gy = (int)std::floor(worldMouseY); }
        if (gx>=0 && gy>=0 && gx<worldMap_.width && gy<worldMap_.height) {
            size_t idx = (size_t)gy * worldMap_.width + gx;
            uint16_t pal = (idx < worldMap_.paletteIndices.size()) ? worldMap_.paletteIndices[idx] : 0u;
            int biomeId = (pal >= 2) ? (int)pal - 2 : -1; // -1 = eau
            std::string biomeNameStr; uint32_t biomeRGB = 0x202020; bool isWater = (biomeId < 0);
            if (!isWater) {
                if (biomeId >= 0 && biomeId < (int)worldMap_.biomeNames.size()) {
                    const std::string &src = worldMap_.biomeNames[biomeId];
                    biomeNameStr = src.empty()? ("Biome "+std::to_string(biomeId)) : src;
                    if (biomeId < (int)worldMap_.biomeColorsRGB.size()) biomeRGB = worldMap_.biomeColorsRGB[biomeId];
                } else biomeNameStr = "Biome "+std::to_string(biomeId);
            } else {
                if (pal == 0) { biomeNameStr = "Water (Deep)"; biomeRGB = 0x083044; }
                else if (pal == 1) { biomeNameStr = "Water (Shallow)"; biomeRGB = 0x1A5878; }
                else biomeNameStr = "Water";
            }
            float heightVal = (idx < worldMap_.tileHeights.size()) ? worldMap_.tileHeights[idx] : 0.f;
            // Fallback: if height is zero (sparse sampling) try adaptive cell meanHeight covering cursor
            if (heightVal == 0.f && !worldMap_.adaptiveCells.empty()) {
                float wx = worldMouseX; float wy = worldMouseY;
                for (const auto &ac : worldMap_.adaptiveCells) {
                    if (wx >= ac.x && wx < ac.x + ac.w && wy >= ac.y && wy < ac.y + ac.h) { heightVal = ac.meanHeight; break; }
                }
            }
            ImGui::Text("Palette=%u | BiomeId=%d | Name=%s | Height=%.3f", pal, biomeId, biomeNameStr.c_str(), heightVal);
            ImGui::SameLine(); ImGui::TextColored(ImVec4(((biomeRGB>>16)&0xFF)/255.f, ((biomeRGB>>8)&0xFF)/255.f, (biomeRGB&0xFF)/255.f,1.f), "#%06X", biomeRGB);
            if (biomeId >= 0 && idx < worldMap_.tiles.size()) {
                uint16_t tileStored = worldMap_.tiles[idx];
                if (tileStored != (uint16_t)biomeId) ImGui::TextColored(ImVec4(1,0.6f,0,1), "Note: tiles[%zu]=%u (centroid only) -> utilisez paletteIndices.", idx, tileStored);
            }
            // Country debug (locale unique scope)
            {
                uint16_t dbgCountryId = (idx < worldMap_.countries.size()) ? worldMap_.countries[idx] : 0;
                if (dbgCountryId > 0) {
                    std::string cname = "Country " + std::to_string(dbgCountryId);
                    if (!worldMap_.countryInfos.empty()) {
                        for (auto &ci : worldMap_.countryInfos) if (ci.id == dbgCountryId) { if(!ci.name.empty()) cname = ci.name; break; }
                    }
                    uint32_t cRGB = (dbgCountryId < worldMap_.countryColorsRGB.size()) ? worldMap_.countryColorsRGB[dbgCountryId] : 0x606060;
                    ImGui::Text("CountryId=%u (%s)", dbgCountryId, cname.c_str());
                    ImGui::SameLine(); ImGui::TextColored(ImVec4(((cRGB>>16)&0xFF)/255.f, ((cRGB>>8)&0xFF)/255.f, (cRGB&0xFF)/255.f,1.f), "#%06X", cRGB);
                } else ImGui::Text("CountryId=0 (none)");
            }
        }
    }
    ImGui::End();
    ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::run() {
    while (!appWindow_->shouldClose()) {
        double now = glfwGetTime(); double frame = now - lastTime_; lastTime_=now; accumulator_+=frame;
        input_->beginFrame(); appWindow_->poll();
        while (accumulator_>=fixedStep_) { fixedUpdate(fixedStep_); accumulator_-=fixedStep_; }
        render(frame);
        appWindow_->swap(); input_->endFrame();
    }
}

void Application::shutdown() {
    if (farMapRenderer_) { farMapRenderer_->shutdown(); farMapRenderer_.reset(); }
    ShutdownImGui();
    if (input_) input_.reset();
    if (appWindow_) { appWindow_->shutdown(); appWindow_.reset(); }
}
