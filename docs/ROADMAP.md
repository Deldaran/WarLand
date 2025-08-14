# Roadmap (phases)

M0 – Boot (2-3 semaines)
- CMake, structure, toolchain Windows, CI locale
- Hello Window (GLFW + OpenGL + ImGui)
- Time, Log, EventBus, Config minimal

M1 – ECS & Rendu de base (3-4 semaines)
- ECS minimal (Entity/Registry/Transform/Renderable)
- Renderer: meshes simples, shaders, caméra orbitale
- AssetLoader (gltf, textures), ResourceManager
- UI debug (ImGuiLayer)

M2 – Simulation & Monde (4-6 semaines)
- Scheduler pas fixe, Timewarp, Save/Load JSON
- Générateur de carte basique (provinces, routes)
- RoadNetwork + Courier + Messaging (ordres différés)
- MapUI, overlays top-down

M3 – Ville isométrique (6-8 semaines)
- CityController, CityGenerator
- Buildings, Citizens (comportements simples), JobSystem
- UtilityNetworks, TrafficSystem simplifiés
- Caméra isométrique, CityUI

M4 – Personnage (6-8 semaines)
- CharacterController (clic-déplacement), NavMesh local
- Interaction, Inventory, Animation de base
- CharacterCamera, CharacterUI

M5 – IA & Navigation avancées (6-8 semaines)
- Pathfinding multi-niveaux (Monde/Ville/Intérieur)
- GOAP/BehaviorTree pour citoyens et PNJ

M6 – Audio, polish, optimisation (continu)
- OpenAL Soft, mixage basique
- LOD sim/rendu, streaming d’entités, profiling

Livrables clés
- Prototype jouable: fin M2
- Slice vertical ville: fin M3
- Slice vertical personnage: fin M4
