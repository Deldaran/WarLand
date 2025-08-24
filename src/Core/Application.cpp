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
#include "../Engine/WorldGen/TerrainNoise.h"

// Global minimal terrain config/state for the viewer
static TerrainNoiseConfig gNoiseCfg; // defaults defined in header
static uint64_t gNoiseSeed = 123456789ull;
static bool gRealtimeGen = true;

static void SetupImGui(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 450");
    // Charger fonte fantasy 8-bit si disponible (placer un .ttf dans assets/fonts)
    std::filesystem::path fontPath = std::filesystem::path("assets/fonts")/"Movistar Text Regular.ttf"; // updated filename
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

    // Minimal synthetic map (no L1)
    worldMap_.width = 512; worldMap_.height = 512; worldMap_.worldMaxX = 512.f; worldMap_.worldMaxY = 512.f;
    worldMap_.paletteIndices.assign((size_t)worldMap_.width*worldMap_.height, 2u);
    worldMeshRenderer_ = std::make_unique<SimpleWorldMeshRenderer>();
    worldMeshRenderer_->init(&worldMap_);
    // First terrain
    TerrainNoise::Generate(worldMap_, gNoiseSeed, gNoiseCfg);
    worldMeshRenderer_->rebuild(&worldMap_);

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
    // GPU tess params (shared with UI)
    static bool autoTessRange = false; static float tessNearD = 250.f, tessFarD = 3000.f; static int tessMinL=1, tessMaxL=24, tessBaseStep=8;
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
        // Échantillonner hauteur mesh sous la caméra et empêcher la caméra de passer au travers
        auto sampleHeight01At = [&](float wx, float wy){
            if (worldMap_.width<=0 || worldMap_.height<=0 || worldMap_.tileHeights.empty()) return 0.0f;
            float gx = (worldMap_.worldMaxX>0)? (wx / worldMap_.worldMaxX) * (float)(worldMap_.width - 1) : wx;
            float gy = (worldMap_.worldMaxY>0)? (wy / worldMap_.worldMaxY) * (float)(worldMap_.height - 1) : wy;
            // clamp to grid
            if (!(gx>=0 && gy>=0 && gx<=worldMap_.width-1 && gy<=worldMap_.height-1)){
                gx = std::clamp(gx, 0.0f, (float)(worldMap_.width - 1));
                gy = std::clamp(gy, 0.0f, (float)(worldMap_.height - 1));
            }
            int x0 = (int)floorf(gx); int y0 = (int)floorf(gy);
            int x1 = std::min(x0+1, worldMap_.width-1);
            int y1 = std::min(y0+1, worldMap_.height-1);
            float tx = gx - (float)x0; float ty = gy - (float)y0;
            size_t i00 = (size_t)y0*worldMap_.width + x0;
            size_t i10 = (size_t)y0*worldMap_.width + x1;
            size_t i01 = (size_t)y1*worldMap_.width + x0;
            size_t i11 = (size_t)y1*worldMap_.width + x1;
            float h00 = (i00<worldMap_.tileHeights.size())? worldMap_.tileHeights[i00] : 0.f;
            float h10 = (i10<worldMap_.tileHeights.size())? worldMap_.tileHeights[i10] : 0.f;
            float h01 = (i01<worldMap_.tileHeights.size())? worldMap_.tileHeights[i01] : 0.f;
            float h11 = (i11<worldMap_.tileHeights.size())? worldMap_.tileHeights[i11] : 0.f;
            float hx0 = h00 + (h10 - h00) * tx;
            float hx1 = h01 + (h11 - h01) * tx;
            return hx0 + (hx1 - hx0) * ty; // 0..globalAmplitude
        };
        float ground01 = sampleHeight01At(camX, camY);
        float heightScaleZ = (worldMeshRenderer_? worldMeshRenderer_->heightScale() : 1.0f);
        // Convertit en Z en soustrayant la base min pour être cohérent avec le shader
        float baseMin = worldMap_.landMinHeight;
        float groundZ = std::max(0.0f, (ground01 - baseMin)) * heightScaleZ; // unités monde
        // Anticipe la pente en échantillonnant un point devant la caméra
        float fxYaw = sinf(camYaw), fyYaw = -cosf(camYaw);
        float aheadDist = std::max(1.0f, 6.0f * dynamicMinHeight); // quelques mètres devant
        float groundAhead01 = sampleHeight01At(camX + fxYaw * aheadDist, camY + fyYaw * aheadDist);
        float groundAheadZ = std::max(0.0f, (groundAhead01 - baseMin)) * heightScaleZ;
        groundZ = std::max(groundZ, groundAheadZ);
        // Limite zoom: ne jamais descendre sous 1.70 m au-dessus du sol (epsilon pour éviter clipping)
        {
            float eps = std::max(0.25f * dynamicMinHeight, 0.05f);
            float minCamZ = groundZ + eps;
            float limitZ = groundZ + dynamicMinHeight;
            if (minCamZ > limitZ) minCamZ = limitZ; // sécurité
            if (camHeight < limitZ) camHeight = std::max(camHeight, minCamZ);
        }
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
    // no verbose logs in minimal viewer
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
        // no verbose logs in minimal viewer
        if (dir.x!=0 || dir.y!=0) {
            dir = glm::normalize(dir);
            camX += dir.x * moveSpeed;
            camY += dir.y * moveSpeed;
            // no verbose logs in minimal viewer
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
    float fovNear = 58.f, fovFar = 63.f;
    float fovBlend = (camHeight - dynamicMinHeight) / (maxHeight - dynamicMinHeight); if (fovBlend<0) fovBlend=0; if (fovBlend>1) fovBlend=1;
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
    bool showFar = false;
    // Compute view width in world units for adaptive/tess logic
    float viewWorldWidth_now = (float)w / totalZoom;
    if (worldMeshRenderer_) {
        bool force = !showFar; // toujours visible quand la carte 2D est cachée
    worldMeshRenderer_->setCamera(camX, camY);
        // Auto-tie tess near/far to zoom (view width)
        if (worldMeshRenderer_->tessEnabled()) {
            if (autoTessRange) {
                float nearF = std::clamp(viewWorldWidth_now * 0.15f, 10.f, 4000.f);
                float farF  = std::clamp(viewWorldWidth_now * 1.50f, nearF + 50.f, 20000.f);
                tessNearD = nearF; tessFarD = farF;
                worldMeshRenderer_->setTessParams(tessNearD, tessFarD, tessMinL, tessMaxL, tessBaseStep);
            } else {
                // Ensure renderer has latest manual values
                worldMeshRenderer_->setTessParams(tessNearD, tessFarD, tessMinL, tessMaxL, tessBaseStep);
            }
        }
        if (meshWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        worldMeshRenderer_->render(&worldMap_, vp, totalZoom, force);
        if (meshWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

    // No HUD

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

    // No labels

    ImGui::Begin("Warland");
    ImGui::Text("Map %dx%d", worldMap_.width, worldMap_.height);
    ImGui::Text("Zoom: %.2f  Height: %.3f  Pitch: %.1f  Mode: %s", zoomFactor, camHeight, camPitchDeg, camPitchDeg>fpsPitchSwitch? "TopDown":"FPS");
    if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Wireframe mesh", &meshWireframe);
        bool hshade = worldMeshRenderer_? worldMeshRenderer_->heightShading() : true;
        if (ImGui::Checkbox("Height shading", &hshade)) { if (worldMeshRenderer_) worldMeshRenderer_->setHeightShading(hshade); }
        ImGui::Checkbox("Debug axes monde", &showAxes);
        static bool adaptive = false; static float radius = 200.f; static int refine = 4; static int outerStep = 4;
        if (ImGui::Checkbox("Adaptive refinement (near camera)", &adaptive)) { if(worldMeshRenderer_) worldMeshRenderer_->setAdaptive(adaptive, radius, refine, outerStep); }
        if (adaptive) {
            bool r0=false, r1=false, r2=false;
            r0 |= ImGui::SliderFloat("Refine radius (u)", &radius, 20.f, 1200.f, "%.0f");
            r1 |= ImGui::SliderInt("Refine factor", &refine, 2, 8);
            r2 |= ImGui::SliderInt("Outer step", &outerStep, 2, 16);
            if ((r0||r1||r2) && worldMeshRenderer_) worldMeshRenderer_->setAdaptive(adaptive, radius, refine, outerStep);
        }
        if (worldMeshRenderer_) {
            bool tess = worldMeshRenderer_->tessEnabled();
            if (ImGui::Checkbox("GPU Tessellation", &tess)) worldMeshRenderer_->setTessEnabled(tess);
            if (tess) {
                ImGui::Checkbox("Auto tess near/far", &autoTessRange);
                if (!autoTessRange) {
                    bool c0=false,c1=false;
                    c0|=ImGui::SliderFloat("Tess near (u)", &tessNearD, 10.f, 4000.f, "%.0f");
                    c1|=ImGui::SliderFloat("Tess far (u)", &tessFarD, 100.f, 20000.f, "%.0f");
                    if (c0||c1) { if (tessFarD < tessNearD+50.f) tessFarD = tessNearD+50.f; worldMeshRenderer_->setTessParams(tessNearD, tessFarD, tessMinL, tessMaxL, tessBaseStep); }
                }
                bool c2=false,c3=false,c4=false;
                c2|=ImGui::SliderInt("Tess min level", &tessMinL, 1, 8);
                c3|=ImGui::SliderInt("Tess max level", &tessMaxL, 4, 32);
                c4|=ImGui::SliderInt("Tess base step", &tessBaseStep, 2, 32);
                if (c2||c3||c4) worldMeshRenderer_->setTessParams(tessNearD, tessFarD, tessMinL, tessMaxL, tessBaseStep);
                ImGui::Text("View width: %.0f u | Near: %.0f | Far: %.0f", viewWorldWidth_now, tessNearD, tessFarD);
            }
        }
    }
    if (ImGui::CollapsingHeader("Camera / Zoom")) {
        ImGui::SliderFloat("Zoom ease power", &zoomEasePower, 1.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Scroll min step", &scrollMinStep, 0.001f, 0.05f, "%.3f");
        ImGui::SliderFloat("Scroll max step", &scrollMaxStep, 0.05f, 0.25f, "%.2f");
    }
    if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool dirty=false;
        ImGui::Checkbox("Realtime", &gRealtimeGen);
        ImGui::Text("Seed: %llu", (unsigned long long)gNoiseSeed);
        if (ImGui::Button("Randomize seed")) { gNoiseSeed = (((uint64_t)rand()<<32) ^ (uint64_t)rand() ^ (uint64_t)glfwGetTime()); dirty=true; }
        // Vertical scale for mesh
        if (worldMeshRenderer_) {
            float hs = worldMeshRenderer_->heightScale();
            if (ImGui::SliderFloat("Height scale (Z)", &hs, 1.0f, 5000.0f, "%.0f", ImGuiSliderFlags_Logarithmic)) {
                worldMeshRenderer_->setHeightScale(hs);
            }
        }
        dirty |= ImGui::SliderInt("Octaves", &gNoiseCfg.octaves, 1, 9);
        dirty |= ImGui::SliderFloat("Base freq", &gNoiseCfg.baseFrequency, 0.00005f, 0.01f, "%.5f", ImGuiSliderFlags_Logarithmic);
        dirty |= ImGui::SliderFloat("Lacunarity", &gNoiseCfg.lacunarity, 1.5f, 3.0f, "%.2f");
        dirty |= ImGui::SliderFloat("Gain", &gNoiseCfg.gain, 0.2f, 0.9f, "%.2f");
        dirty |= ImGui::SliderFloat("Amplitude", &gNoiseCfg.globalAmplitude, 0.01f, 1.0f, "%.2f");
        dirty |= ImGui::SliderFloat("Sea level", &gNoiseCfg.seaLevel, 0.0f, 1.0f, "%.2f");
        dirty |= ImGui::SliderInt("Blur passes", &gNoiseCfg.blurPasses, 0, 6);
        dirty |= ImGui::SliderFloat("Slope X", &gNoiseCfg.slopeX, -0.5f, 0.5f, "%.2f");
        dirty |= ImGui::SliderFloat("Slope Y", &gNoiseCfg.slopeY, -0.5f, 0.5f, "%.2f");
        if (dirty && gRealtimeGen) { TerrainNoise::Generate(worldMap_, gNoiseSeed, gNoiseCfg); if (worldMeshRenderer_) worldMeshRenderer_->rebuild(&worldMap_); }
        if (!gRealtimeGen && ImGui::Button("Generate")) { TerrainNoise::Generate(worldMap_, gNoiseSeed, gNoiseCfg); if (worldMeshRenderer_) worldMeshRenderer_->rebuild(&worldMap_); }
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
    if (worldMeshRenderer_) { worldMeshRenderer_->shutdown(); worldMeshRenderer_.reset(); }
    ShutdownImGui();
    if (input_) input_.reset();
    if (appWindow_) { appWindow_->shutdown(); appWindow_.reset(); }
}
