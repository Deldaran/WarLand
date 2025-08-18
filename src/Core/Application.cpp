#include "Application.h"

#include <cstdio>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <optional>
#include <filesystem>
#include <fstream>
#include <string>

#include "../Platform/Window.h"
#include "../Platform/Input.h"
#include "../Engine/Simulation/Scheduler.h"
#include "../Tools/AssetPacker/AzgaarImporter.h"
#include "../Engine/WorldGen/BiomeGenerator.h"

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
        worldMeshRenderer_ = std::make_unique<SimpleWorldMeshRenderer>();
        if (worldMeshRenderer_->init(&worldMap_)) {
            std::fprintf(stderr, "[Load] SimpleWorldMeshRenderer (L2) initialisé\n");
        } else {
            std::fprintf(stderr, "[Load][ERREUR] SimpleWorldMeshRenderer init failed\n");
        }

        // Procedural biome-based generation (heights enrichment) with global seed
        uint64_t globalSeed = 123456789123ull; // TODO: lire depuis config fichier / sauvegarde
        // Pour chaque biome référencé dans paletteIndices, assurer une seed puis générer hauteur si pas déjà présente
        std::vector<bool> biomeSeen(worldMap_.biomeNames.size(), false);
        for (size_t i=0; i<worldMap_.paletteIndices.size(); ++i) {
            uint16_t pal = worldMap_.paletteIndices[i];
            int bId = (pal >= 2) ? (int)pal - 2 : -1;
            if (bId >= 0 && bId < (int)biomeSeen.size()) biomeSeen[bId] = true;
        }
        for (int b=0; b<(int)biomeSeen.size(); ++b) if (biomeSeen[b]) {
            BiomeGenerator::EnsureBiomeSeed(worldMap_, b, globalSeed);
            BiomeGenConfig cfg = BiomeGenerator::DefaultConfigFor(b < (int)worldMap_.biomeNames.size() ? worldMap_.biomeNames[b] : std::string(""));
            BiomeGenerator::GenerateHeightsForBiome(worldMap_, b, cfg);
        }
        std::fprintf(stderr, "[Gen] Biome procedural heights generated.\n");
    }

    lastTime_=glfwGetTime(); accumulator_=0.0; fixedStep_=1.0/60.0; return true;
}

void Application::fixedUpdate(double dt) {
    if (scheduler_) scheduler_->updateFixed(dt);
}

void Application::render(double /*dt*/) {
    int w, h; appWindow_->framebufferSize(w, h); glViewport(0,0,w,h); glClearColor(0.08f,0.09f,0.11f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
    static float camX=0.f, camY=0.f; 
    static float baseZoom=1.f;             // base fit width
    static float zoomFactor=1.f;           // user scroll factor (>1 close)
    // Camera always perspective now
    static float camYaw = 0.f;             // yaw=0 looks toward -Y (screen up)
    static float camPitchDeg = 89.f;       // starts top-down
    static float mouseLastX = 0.f, mouseLastY = 0.f; static bool hadMouseLast=false;
    static const float pitchTopDown = 89.f; // top view
    static const float pitchFPS      = 18.f; // low angle for FPS
    static const float maxHeight     = 1200.f; // high altitude overview
    static float       dynamicMinHeight = 5.f; // will be overridden by human scale computation
    static const float yawSensitivity   = 0.0030f;
    static const float pitchSensitivity = 0.0020f * 57.2957795f;
    static const float fpsPitchSwitch   = 60.f; // below this pitch -> FPS style movement
    static const float hideMapHeight    = 180.f; // below this height hide L1 map
    static bool meshWireframe = false; // affichage maillage L2
    static int lastW=0, lastH=0; 
    const float mapWidthUnits = worldMap_.worldMaxX > 0 ? worldMap_.worldMaxX : (float)worldMap_.width;
    const float mapHeightUnits = worldMap_.worldMaxY > 0 ? worldMap_.worldMaxY : (float)worldMap_.height;
    // Recalcul si première frame ou resize
    static bool camCentered = false;
    if (lastW!=w || lastH!=h) {
        baseZoom = (mapWidthUnits>0)? (float)w / mapWidthUnits : 1.f;
        lastW=w; lastH=h;
        // Center camera on map
        camX = mapWidthUnits * 0.5f;
        camY = mapHeightUnits * 0.5f;
        camCentered = true;
    }
    // Compute synthetic totalZoom (for layer logic) still used by renderers
    float totalZoom = baseZoom * zoomFactor;

    // --- City label configuration (runtime tunable via ImGui) ---
    static bool  cfgCityEnable          = true;
    static bool  cfgCityDynamicScale    = true;
    static bool  cfgCityDistanceFade    = true;
    static bool  cfgCityAltitudeFade    = true;
    static float cfgCityMinPx           = 10.f;
    static float cfgCityMaxPx           = 26.f;
    static float cfgCityNearDist        = 80.f;     // units
    static float cfgCityFarDist         = 3000.f;   // units used for scale interpolation
    static float cfgCityBaseMaxDist     = 3500.f;   // base hard cull distance at ground
    static float cfgCityHighAltFactor   = 0.25f;    // fraction of base max dist kept at highest altitude
    static float cfgCityAltitudeFadeStart = 0.55f;  // altitudeT start fade
    static float cfgCityAltitudeFadeEnd   = 0.90f;  // altitudeT end fade
    static int   cfgCityMaxLabels       = 400;      // limit drawn
    static float cfgCityMinAlpha        = 0.07f;    // below -> skip

    // Human scale adaptation (configurable)
    static float cfgWorldKmWidth = 2700.f;      // largeur réelle carte (km)
    static float cfgEyeHeightMeters = 1.70f;    // hauteur œil humaine (m)
    float worldKmWidth = cfgWorldKmWidth;
    float kmPerUnit = (mapWidthUnits>0) ? (worldKmWidth / mapWidthUnits) : 1.f;
    float humanEyeHeightKm = cfgEyeHeightMeters / 1000.f; // m -> km
    float humanEyeHeightUnits = (kmPerUnit>0.f) ? (humanEyeHeightKm / kmPerUnit) : 0.002f;
    if (humanEyeHeightUnits < 0.0001f) humanEyeHeightUnits = 0.0001f; // avoid zero
    dynamicMinHeight = humanEyeHeightUnits; // override previous fixed min

    // Derive camera height (inverse relation with zoomFactor) using log curve for smoother mid-range
    float zf = zoomFactor; if (zf < 0.05f) zf = 0.05f; if (zf > 300.f) zf = 300.f;
    static float zoomEasePower = 2.2f; // >1 -> ralenti fortement la descente proche du sol
    float tHeightLin = logf(zf) / logf(300.f); // 0 .. 1 (linéaire dans l'espace log du zoom)
    if (tHeightLin < 0.f) tHeightLin = 0.f; if (tHeightLin>1.f) tHeightLin=1.f;
    // Ease-out: t' = 1 - (1 - t)^p  (dérivée -> 0 quand t->1)
    float tHeight = 1.f - powf(1.f - tHeightLin, zoomEasePower);
    if (tHeight < 0.f) tHeight = 0.f; if (tHeight>1.f) tHeight=1.f;
    float camHeight = maxHeight - tHeight * (maxHeight - dynamicMinHeight);
    if (camHeight < dynamicMinHeight) camHeight = dynamicMinHeight;
    float altitudeT = (camHeight - dynamicMinHeight) / (maxHeight - dynamicMinHeight); if (altitudeT<0) altitudeT=0; if (altitudeT>1) altitudeT=1;

    // Target pitch based on height (higher -> top-down)
    float tPitch = (camHeight - dynamicMinHeight) / (maxHeight - dynamicMinHeight); if (tPitch<0) tPitch=0; if (tPitch>1) tPitch=1;
    float targetPitch = pitchFPS + tPitch * (pitchTopDown - pitchFPS);

    // Mouse look only when below top-down threshold or user forces by holding mouse left
    double mxNow,myNow; input_->mousePosition(mxNow,myNow);
    if (!hadMouseLast) { mouseLastX=(float)mxNow; mouseLastY=(float)myNow; hadMouseLast=true; }
    bool allowYaw = (targetPitch < pitchTopDown - 5.f); // avoid yaw changes in near top-down to keep north up
    if (input_->isDown(GLFW_MOUSE_BUTTON_LEFT) && allowYaw) {
        float dx = (float)mxNow - mouseLastX;
        float dy = (float)myNow - mouseLastY;
        camYaw += dx * yawSensitivity;
        camPitchDeg -= dy * pitchSensitivity; // souris haut -> regarde haut
        if (camYaw > 3.14159265f) camYaw -= 6.2831853f; else if (camYaw < -3.14159265f) camYaw += 6.2831853f;
        if (camPitchDeg < pitchFPS) camPitchDeg = pitchFPS; if (camPitchDeg > pitchTopDown) camPitchDeg = pitchTopDown;
    }
    mouseLastX=(float)mxNow; mouseLastY=(float)myNow;
    // Converge pitch toward target if user not actively dragging
    if (!(input_->isDown(GLFW_MOUSE_BUTTON_LEFT) && allowYaw)) {
        camPitchDeg = camPitchDeg * 0.90f + targetPitch * 0.10f;
    }
    if (camPitchDeg < pitchFPS) camPitchDeg = pitchFPS; if (camPitchDeg > pitchTopDown) camPitchDeg = pitchTopDown;
    if (!allowYaw) camYaw = 0.f; // lock north-up in top-down region

    // Movement (ZQSD)
    bool keyFwd  = input_->isDown(GLFW_KEY_W) || input_->isDown(GLFW_KEY_Z);
    bool keyBack = input_->isDown(GLFW_KEY_S);
    bool keyLeft = input_->isDown(GLFW_KEY_A) || input_->isDown(GLFW_KEY_Q);
    bool keyRight= input_->isDown(GLFW_KEY_D);
    // Movement speed revisité (échelle humaine accrue):
    bool keySprint = input_->isDown(GLFW_KEY_LEFT_SHIFT) || input_->isDown(GLFW_KEY_RIGHT_SHIFT);
    float humanWalkKmPerSec = 0.0010f; // ~1.0 m/s très lent pour donner échelle
    float humanRunKmPerSec  = 0.0028f; // ~2.8 m/s (Shift)
    float humanSpeedKmPerSec = keySprint ? humanRunKmPerSec : humanWalkKmPerSec;
    float humanSpeedUnitsPerSec = (kmPerUnit>0.f)? (humanSpeedKmPerSec / kmPerUnit) : 1.f;
    float overviewSpeedUnitsPerSec = mapWidthUnits * 0.25f; // altitude haute
    float heightT = (camHeight - dynamicMinHeight) / (maxHeight - dynamicMinHeight); if (heightT<0) heightT=0; if (heightT>1) heightT=1;
    float localTransitionHeight = dynamicMinHeight * 1200.f + 0.0001f; // zone où on quitte la lenteur extrême
    float groundT = (camHeight - dynamicMinHeight) / localTransitionHeight; if (groundT<0) groundT=0; if (groundT>1) groundT=1;
    // Exponent + offset très faible au sol
    float gamma = 2.8f; float minFactor = 0.03f; // 3% vitesse marche au ras
    float nearGroundFactor = minFactor + (1 - minFactor) * powf(groundT, gamma);
    float scaledHumanSpeed = humanSpeedUnitsPerSec * nearGroundFactor;
    float currentSpeedUnitsPerSec = scaledHumanSpeed * (1.0f - heightT) + overviewSpeedUnitsPerSec * heightT;
    float moveSpeed = currentSpeedUnitsPerSec * (float)fixedStep_;
    if (camPitchDeg > fpsPitchSwitch) {
        // Top-down style (screen axes)
        if (keyFwd)  camY -= moveSpeed;
        if (keyBack) camY += moveSpeed;
    // Inversion X pour que Q (gauche) fasse défiler la carte vers la gauche visuelle
    if (keyLeft) camX += moveSpeed;  // anciennement -
    if (keyRight)camX -= moveSpeed;  // anciennement +
        if (keyFwd||keyBack||keyLeft||keyRight) {
            std::fprintf(stderr,
                "[MoveTop] F:%d B:%d L:%d R:%d pitch=%.1f yaw=%.2f cam=(%.1f,%.1f) move=%.2f\n",
                (int)keyFwd,(int)keyBack,(int)keyLeft,(int)keyRight, camPitchDeg, camYaw, camX, camY, moveSpeed);
            std::fflush(stderr);
        }
    } else {
    float yaw = camYaw;
    float fx = sinf(yaw);
    float fy = -cosf(yaw);
    float rx =  fy;
    float ry =  fx;
    glm::vec2 dir(0);
        if (keyFwd)  dir += glm::vec2(fx,fy);
        if (keyBack) dir -= glm::vec2(fx,fy);
        if (keyLeft) dir -= glm::vec2(rx,ry);
        if (keyRight)dir += glm::vec2(rx,ry);
        if (keyFwd || keyBack || keyLeft || keyRight) {
            std::fprintf(stderr,
                "[MoveFPS] F:%d B:%d L:%d R:%d yaw=%.2f pitch=%.2f fx=%.3f fy=%.3f rx=%.3f ry=%.3f preDir=(%.3f,%.3f)\n",
                (int)keyFwd,(int)keyBack,(int)keyLeft,(int)keyRight, camYaw, camPitchDeg,
                fx, fy, rx, ry, dir.x, dir.y);
            std::fflush(stderr);
        }
        if (dir.x!=0 || dir.y!=0) {
            dir = glm::normalize(dir);
            camX += dir.x * moveSpeed;
            camY += dir.y * moveSpeed;
            std::fprintf(stderr,"[MoveFPS] finalDir=(%.3f,%.3f) cam=(%.2f,%.2f) speed=%.2f\n", dir.x, dir.y, camX, camY, moveSpeed);
            std::fflush(stderr);
        }
    }

    // Scroll zoom ancré sur la position souris (point monde sous le curseur reste stable)
    double mx, my; input_->mousePosition(mx, my); // coords fenêtre en pixels
    double scroll = input_->scrollDelta();
    // Adaptive scroll: pas très fin proche du sol, plus gros en altitude
    static float scrollMinStep = 0.010f; // multiplicateur - proche sol -> 1 +/- 0.01
    static float scrollMaxStep = 0.11f;  // multiplicateur haut -> 1 +/- 0.11 (≈ précédent 10%)
    float step = scrollMinStep + (scrollMaxStep - scrollMinStep) * altitudeT; if (step < 0.001f) step = 0.001f; if (step > 0.5f) step = 0.5f;
    if (scroll != 0.0) {
        float mult = (scroll > 0.0) ? (1.f + step) : (1.f - step);
        if (mult < 0.01f) mult = 0.01f;
        zoomFactor *= mult;
        if (zoomFactor < 0.05f) zoomFactor = 0.05f; if (zoomFactor > 300.f) zoomFactor = 300.f;
    }
    glm::mat4 vp;
    // Perspective always + FOV/clip dynamiques
    float aspect = (float)w / (float)h;
    float fovBlend = (camHeight - dynamicMinHeight) / (maxHeight - dynamicMinHeight); if (fovBlend<0) fovBlend=0; if (fovBlend>1) fovBlend=1;
    float fovNear = 58.f, fovFar = 63.f;
    float fov = fovNear * (1 - fovBlend) + fovFar * fovBlend;
    float nearPlane = camHeight * 0.05f; float minNear = dynamicMinHeight * 0.02f; if (nearPlane < minNear) nearPlane = minNear; if (nearPlane < 0.0000005f) nearPlane = 0.0000005f;
    float farPlaneLow = 600.f; float farPlaneHigh = 20000.f; float farPlane = farPlaneLow * (1 - fovBlend) + farPlaneHigh * fovBlend;
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
    float pitchRad = glm::radians(camPitchDeg);
    float yaw = camYaw;
    float fx3 = sinf(yaw) * cosf(pitchRad); float fy3 = -cosf(yaw) * cosf(pitchRad); float fz3 = -sinf(pitchRad);
    glm::vec3 eye(camX, camY, camHeight); glm::vec3 forward(fx3,fy3,fz3); glm::vec3 center = eye + forward;
    glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0,0,1));
    // No Y reflection now (keep natural right-handed: +Y up). Movement logic keeps continuity.
    vp = proj * view;
    bool showFar = (camHeight > hideMapHeight && camPitchDeg > fpsPitchSwitch+5.f);
    if (showFar && farMapRenderer_) farMapRenderer_->render(&worldMap_, vp, totalZoom);
    if (worldMeshRenderer_) {
        bool force = !showFar; // toujours visible quand la carte 2D est cachée
        if (meshWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        worldMeshRenderer_->render(&worldMap_, vp, totalZoom, force);
        if (meshWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

    // Delegate HUD input + drawing to MapHUD
    mapHUD_.handleInput(input_.get());
    mapHUD_.draw(farMapRenderer_.get(), worldMap_);

    // Calque labels monde (avant fenêtre UI) : utiliser draw list background pour rester sous les fenêtres
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // Ancien worldToScreen 2D (utile debug axes top-down)
    auto worldToScreen2D = [&](float wx, float wy){ ImVec2 p; p.x = (wx - camX)*totalZoom; p.y = (wy - camY)*totalZoom; return p; };
    // Projection perspective correcte (wx,wy,wz=0) -> écran pixels
    auto projectToScreen = [&](float wx, float wy, float wz)->std::optional<ImVec2>{
        glm::vec4 clip = vp * glm::vec4(wx, wy, wz, 1.0f);
        if (clip.w <= 0.00001f) return std::nullopt; // derrière caméra
        glm::vec3 ndc = glm::vec3(clip) / clip.w; // -1..1
        // Optionnel: on garde même hors frustum partiel pour anti-pop; on peut tester ndc.z si besoin
        float sx = (ndc.x * 0.5f + 0.5f) * (float)w;
        float sy = (-ndc.y * 0.5f + 0.5f) * (float)h; // y écran down
        return ImVec2(sx, sy);
    };

    // Debug axes overlay (optionnel)
    static bool showAxes = false; // toggled via ImGui plus bas
    if (showAxes) { // debug 2D approx (non perspective)
        float axisLen = 250.f; // unités monde
        ImVec2 o  = worldToScreen2D(0.f, 0.f);
        ImVec2 px = worldToScreen2D(axisLen, 0.f);
        ImVec2 py = worldToScreen2D(0.f, axisLen);
        // +X rouge, +Y vert (rappel : +Y va vers le BAS de l'écran en mode ortho)
        dl->AddLine(o, px, IM_COL32(255,80,80,255), 2.f);
        dl->AddLine(o, py, IM_COL32(80,255,80,255), 2.f);
        // flèches
        dl->AddCircleFilled(px, 4.f, IM_COL32(255,80,80,255));
        dl->AddCircleFilled(py, 4.f, IM_COL32(80,255,80,255));
        ImGuiIO& ioDbg = ImGui::GetIO(); ImFont* fontDbg = ioDbg.Fonts->Fonts.empty()? nullptr : ioDbg.Fonts->Fonts[0];
        if (fontDbg) {
            dl->AddText(fontDbg, 14.f, ImVec2(px.x+4, px.y), IM_COL32(255,120,120,255), "+X");
            dl->AddText(fontDbg, 14.f, ImVec2(py.x+4, py.y), IM_COL32(120,255,120,255), "+Y (descend)");
        }
    }

    // Affichage noms des pays (via HUD toggle)
    if (mapHUD_.showCountryNames()) {
        for (auto &ci : worldMap_.countryInfos) {
            if (ci.id<=0) continue;
            if (farMapRenderer_ && farMapRenderer_->isCountryNeutral((uint16_t)ci.id)) continue;
            auto pOpt = projectToScreen(ci.x, ci.y, 0.0f);
            if (!pOpt) continue;
            ImVec2 pos = *pOpt;
            if (pos.x < -300 || pos.y < -200 || pos.x > (float)w+300 || pos.y > (float)h+200) continue;
            float baseSize = 22.0f; float scale = (worldMap_.countryInfos.size()>40)?0.8f:1.0f;
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
    if (mapHUD_.showCityNames()) {
        if (cfgCityEnable) {
            float sx = (worldMap_.width>1 && worldMap_.worldMaxX>0)? worldMap_.worldMaxX / (float)(worldMap_.width -1) : 1.f;
            float sy = (worldMap_.height>1 && worldMap_.worldMaxY>0)? worldMap_.worldMaxY / (float)(worldMap_.height-1) : 1.f;
            struct CityDraw { ImVec2 screen; float dist; float alpha; float size; const char* name; };
            std::vector<CityDraw> candidates; candidates.reserve(worldMap_.places.size());
            // Distance maximale dynamique selon altitude
            float dynamicMaxDist = cfgCityBaseMaxDist * ((1.f - altitudeT) + altitudeT * cfgCityHighAltFactor);
            for (auto &pl : worldMap_.places) {
                if (pl.name.empty()) continue;
                float wx = pl.x * sx; float wy = pl.y * sy; float wz = 0.0f; // TODO: utiliser hauteur terrain future
                // Distance au camera (sol)
                float dx = wx - camX; float dy = wy - camY; float dist2 = dx*dx + dy*dy; float dist = sqrtf(dist2);
                if (dist > dynamicMaxDist) continue; // hard cull
                auto pOpt = projectToScreen(wx, wy, wz);
                if (!pOpt) continue; ImVec2 pos = *pOpt;
                if (pos.x < -120 || pos.y < -80 || pos.x > (float)w+120 || pos.y > (float)h+120) continue;
                float sizePx = 14.f;
                float alpha = 1.f;
                float farDistActive = std::min(cfgCityFarDist, dynamicMaxDist);
                if (cfgCityDynamicScale) {
                    float tD = (dist - cfgCityNearDist) / (farDistActive - cfgCityNearDist); if (tD < 0.f) tD = 0.f; if (tD>1.f) tD=1.f;
                    float inv = 1.f - tD; // proche -> 1
                    sizePx = cfgCityMinPx + (cfgCityMaxPx - cfgCityMinPx) * inv;
                    if (cfgCityDistanceFade) alpha *= (0.35f + 0.65f * inv); // un peu de fade distance
                }
                if (cfgCityAltitudeFade) {
                    float aFade = 1.f;
                    if (altitudeT > cfgCityAltitudeFadeStart) {
                        float tA = (altitudeT - cfgCityAltitudeFadeStart) / (cfgCityAltitudeFadeEnd - cfgCityAltitudeFadeStart);
                        if (tA < 0.f) tA=0.f; if (tA>1.f) tA=1.f;
                        aFade = 1.f - tA;
                    }
                    alpha *= aFade;
                }
                if (alpha < cfgCityMinAlpha) continue;
                candidates.push_back({ pos, dist, alpha, sizePx, pl.name.c_str() });
            }
            // Trier par distance (près -> loin) pour que proche recouvre
            std::sort(candidates.begin(), candidates.end(), [](const CityDraw&a, const CityDraw&b){ return a.dist < b.dist; });
            if ((int)candidates.size() > cfgCityMaxLabels) candidates.resize(cfgCityMaxLabels);
            ImGuiIO& io = ImGui::GetIO(); ImFont* font = io.Fonts->Fonts.empty()? nullptr : io.Fonts->Fonts[0];
            static const ImVec2 offsC[8] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
            for (auto &cd : candidates) {
                float pixelOffset = 8.f;
                const char* txt = cd.name;
                ImVec2 textSize = font? font->CalcTextSizeA(cd.size, FLT_MAX, 0.f, txt) : ImVec2(0,0);
                ImVec2 anchor = ImVec2(cd.screen.x - textSize.x*0.5f, cd.screen.y - textSize.y - pixelOffset);
                unsigned a = (unsigned)std::clamp(cd.alpha*255.f, 0.f,255.f);
                ImU32 colMain = IM_COL32(255,255,255,a);
                ImU32 colOutline = IM_COL32(0,0,0,a);
                ImGui::PushFont(font);
                for (auto &o : offsC) dl->AddText(font, cd.size, ImVec2(anchor.x+o.x, anchor.y+o.y), colOutline, txt);
                dl->AddText(font, cd.size, anchor, colMain, txt);
                ImGui::PopFont();
            }
            // UI config (Labels) plus bas
            static bool addedLabelsSection=false; // placeholder to indicate we added config below
        }
    }

    ImGui::Begin("Warland");
    ImGui::Text("Map %dx%d", worldMap_.width, worldMap_.height);
    ImGui::Text("Countries palette: %zu", worldMap_.countryColorsRGB.size());
    ImGui::Text("ZoomFactor: %.2f Height=%.4f Pitch=%.1f", zoomFactor, camHeight, camPitchDeg);
    ImGui::Text("Mode: %s", camPitchDeg>fpsPitchSwitch? "TopDown" : "FPS");
    ImGui::Text("Yaw: %.2f", camYaw);
    ImGui::Text("Human eye h (u)=%.7f speed=%.6fu/s", dynamicMinHeight, (moveSpeed/(float)fixedStep_));
    if (ImGui::CollapsingHeader("Echelle monde")) {
        ImGui::SliderFloat("World width (km)", &cfgWorldKmWidth, 100.f, 10000.f, "%.0f km");
        ImGui::SliderFloat("Eye height (m)", &cfgEyeHeightMeters, 1.20f, 2.20f, "%.2f m");
        ImGui::Text("kmPerUnit=%.6f", kmPerUnit);
    }
    if (ImGui::CollapsingHeader("Camera / Zoom")) {
        ImGui::SliderFloat("Zoom ease power", &zoomEasePower, 1.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Scroll min step", &scrollMinStep, 0.001f, 0.05f, "%.3f");
        ImGui::SliderFloat("Scroll max step", &scrollMaxStep, 0.05f, 0.25f, "%.2f");
        ImGui::Text("altT=%.3f step=%.4f tLin=%.3f tEase=%.3f camH=%.5f", altitudeT, step, tHeightLin, tHeight, camHeight);
    }
    if (ImGui::CollapsingHeader("Input Debug")) {
        ImGui::Text("Keys W/Z:%d S:%d Q/A:%d D:%d", (int)keyFwd, (int)keyBack, (int)keyLeft, (int)keyRight);
        ImGui::Text("Pitch: %.2f Yaw: %.2f Height: %.1f", camPitchDeg, camYaw, camHeight);
    }
    ImGui::Text("Cam (%.0f, %.0f)", camX, camY);
    ImGui::Text("Places: %zu Roads: %zu", worldMap_.places.size(), worldMap_.roads.size());
    bool showAdaptive = farMapRenderer_ ? farMapRenderer_->showAdaptive() : false; if (ImGui::Checkbox("Grille adaptative (L1)", &showAdaptive)) { if (farMapRenderer_) farMapRenderer_->setShowAdaptive(showAdaptive); }
    bool heightShade = farMapRenderer_ ? farMapRenderer_->heightShading() : false; if (ImGui::Checkbox("Shading hauteur", &heightShade)) { if (farMapRenderer_) farMapRenderer_->setHeightShading(heightShade); }
    bool showCountries = farMapRenderer_ ? farMapRenderer_->showCountries() : true; if (ImGui::Checkbox("Overlay pays", &showCountries)) { if (farMapRenderer_) farMapRenderer_->setShowCountries(showCountries); }
    ImGui::Checkbox("Wireframe mesh (L2)", &meshWireframe);
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
    // Echelle km (réutilise worldKmWidth & kmPerUnit déjà calculés plus haut)
    float viewWorldWidth = (float)w / totalZoom;
    float viewKmWidth = viewWorldWidth * kmPerUnit;
    ImGui::Text("Zoom total (synthetic): %.3f (base=%.3f factor=%.3f)", totalZoom, baseZoom, zoomFactor);
    ImGui::Text("Largeur vue: %.0f u (%.0f km)", viewWorldWidth, viewKmWidth);
    ImGui::Checkbox("Debug axes monde", &showAxes);
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
            }
            float heightVal = (idx < worldMap_.tileHeights.size()) ? worldMap_.tileHeights[idx] : 0.f;
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
            // Country debug
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
    if (ImGui::CollapsingHeader("Labels")) {
        ImGui::Checkbox("Cities enabled", &cfgCityEnable);
        ImGui::Checkbox("Dynamic scale", &cfgCityDynamicScale); ImGui::SameLine(); ImGui::Checkbox("Dist fade", &cfgCityDistanceFade); ImGui::SameLine(); ImGui::Checkbox("Alt fade", &cfgCityAltitudeFade);
        ImGui::SliderFloat("Min px", &cfgCityMinPx, 6.f, 30.f, "%.0f"); ImGui::SliderFloat("Max px", &cfgCityMaxPx, 10.f, 48.f, "%.0f");
        ImGui::SliderFloat("Near dist", &cfgCityNearDist, 10.f, 500.f, "%.0f");
        ImGui::SliderFloat("Far dist", &cfgCityFarDist, 200.f, 8000.f, "%.0f");
        ImGui::SliderFloat("Base max dist", &cfgCityBaseMaxDist, 200.f, 10000.f, "%.0f");
        ImGui::SliderFloat("HighAlt factor", &cfgCityHighAltFactor, 0.05f, 1.0f, "%.2f");
        ImGui::SliderFloat("Alt fade start", &cfgCityAltitudeFadeStart, 0.0f, 0.95f, "%.2f");
        ImGui::SliderFloat("Alt fade end", &cfgCityAltitudeFadeEnd, 0.05f, 1.0f, "%.2f");
        ImGui::SliderInt("Max labels", &cfgCityMaxLabels, 50, 2000);
        ImGui::SliderFloat("Min alpha", &cfgCityMinAlpha, 0.0f, 0.3f, "%.2f");
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
    if (worldMeshRenderer_) { worldMeshRenderer_->shutdown(); worldMeshRenderer_.reset(); }
    ShutdownImGui();
    if (input_) input_.reset();
    if (appWindow_) { appWindow_->shutdown(); appWindow_.reset(); }
}
