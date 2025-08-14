# Tech Stack (proposition initiale)

Objectif: livrer rapidement un prototype jouable (Hello Window + boucle de jeu + entrée clavier/souris), puis itérer vers les 3 niveaux (Monde, Ville, Personnage) avec distances réelles.

- Langage: C++20
- Build: CMake >= 3.20, out-of-source (build/)
- Toolchain cible: Windows (MSVC/Clang-CL). Linux/macOS ultérieurs.
- Rendu (initial): OpenGL 4.5 via GLFW + GLAD
  - Raison: mise en route simple, large compat.
  - Évolution: abstraction Renderer pour basculer vers Vulkan plus tard si besoin.
- Fenêtrage/Entrée: GLFW
- Math: glm
- Physique: Bullet (3D). Pour 2D top (Monde), représenter via couches/masques ou simple collision analytique.
- Navigation: Recast/Detour (navmesh Monde/Ville/Intérieur multi-couches)
- Audio: OpenAL Soft (alternatif: FMOD Studio si licence OK)
- UI runtime: Dear ImGui (debug/outils), UI custom pour HUD
- Scripting/Modding: Lua via sol2, chargement data JSON
- Sérialisation: nlohmann/json (saves JSON compressés zstd au début)
- Formats assets: glTF 2.0 (modèles), KTX2/BasisU (textures), OGG/FLAC (audio), TTF (fonts)
- Tests: Catch2 (unit) + GoogleTest possible; Tests d’intégration headless
- Outils pipeline: Python 3 (scripts d’assets), C# optionnel pour éditeurs

Packages à intégrer progressivement (non encore ajoutés au CMake):
- glm, nlohmann/json, sol2 + Lua, Bullet, Recast/Detour, OpenAL Soft, ImGui, zstd, spdlog

Décisions d’architecture clés
- ECS unique, contrôleurs de niveau (World/City/Character) activent/désactivent des systèmes
- Scheduler déterministe à pas fixe; rendu en delta; LOD sim/rendu
- Messaging intra-univers (ordres par messagers sur réseau routier) et bus d’événements moteur distinct
- Coordonnées en mètres partagées entre niveaux

## vcpkg (C:\vcpkg)

1) Bootstrap + intégration utilisateur
- Ouvrir PowerShell en admin puis exécuter:
  - C:\vcpkg\bootstrap-vcpkg.bat
  - C:\vcpkg\vcpkg integrate install
  - setx VCPKG_ROOT C:\vcpkg

2) Installer les paquets de base
- C:\vcpkg\vcpkg install glfw3:x64-windows glm:x64-windows spdlog:x64-windows nlohmann-json:x64-windows imgui:x64-windows openal-soft:x64-windows bullet3:x64-windows lua:x64-windows sol2:x64-windows zstd:x64-windows glad:x64-windows

3) Configurer CMake avec vcpkg
- cmake -S c:\warland -B c:\warland\build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_BUILD_TYPE=Debug

Note: Recast de vcpkg est marqué deprecated; on intégrera Recast/Detour via FetchContent ultérieurement.
