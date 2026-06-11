# WarLand

God game de grande échelle simulant l'évolution de civilisations de l'âge de pierre jusqu'à la conquête spatiale. Moteur custom en C++20 / OpenGL 4.5, construit de zéro.

---

## Stack technique

| Domaine | Bibliothèque |
|---|---|
| Fenêtre / Contexte OpenGL | GLFW 3 |
| Loader OpenGL | GLAD |
| Math 3D | GLM |
| Entity Component System | EnTT |
| UI debug / jeu | Dear ImGui |
| Textures | stb_image |
| Audio | miniaudio |
| Fonts | FreeType2 |
| Sérialisation / Saves | nlohmann/json |
| Build | CMake 3.20+ |

---

## Architecture

```
┌─────────────────────────────────────────────┐
│              GAME LAYER (WarLand)            │
│  Economy | Politics | Diplomacy | Characters │
├─────────────────────────────────────────────┤
│           SIMULATION ENGINE                  │
│     ECS (EnTT) | Time System | Events        │
├─────────────────────────────────────────────┤
│           RENDER ENGINE                      │
│  Planet Renderer | 2D Overlay | UI | Camera  │
├─────────────────────────────────────────────┤
│              CORE ENGINE                     │
│  Window | Input | ResourceManager | Shaders  │
├─────────────────────────────────────────────┤
│         OpenGL 4.5 / GLFW / GLM              │
└─────────────────────────────────────────────┘
```

**Principe clé :** Simulation et rendu tournent dans des threads séparés. La simulation peut être accélérée (x1 → x100) sans impacter les FPS du rendu.

---

## Structure du projet

```
WarLand/
├── src/
│   ├── core/
│   │   ├── Window.cpp/.h           # GLFW wrapper
│   │   ├── InputManager.cpp/.h
│   │   └── ResourceManager.cpp/.h
│   ├── renderer/
│   │   ├── Shader.cpp/.h           # Compilation GLSL, uniforms
│   │   ├── VertexBuffer.cpp/.h     # VAO/VBO/EBO abstraits
│   │   ├── Texture.cpp/.h
│   │   ├── Camera.cpp/.h           # Caméra orbitale + libre
│   │   ├── planet/
│   │   │   ├── PlanetMesh.cpp/.h   # Icosphère + LOD quadtree
│   │   │   ├── HeightmapGen.cpp/.h # Bruit de Perlin 3D
│   │   │   └── AtmospherePass.cpp/.h
│   │   ├── overlay/
│   │   │   ├── BorderRenderer.cpp/.h   # Frontières sur sphère
│   │   │   ├── IconRenderer.cpp/.h     # Rendu instancié (icônes carte)
│   │   │   └── LayerManager.cpp/.h     # Activation couches visuelles
│   │   └── ui/
│   │       ├── UIManager.cpp/.h
│   │       ├── InfoPanel.cpp/.h
│   │       ├── FilterPanel.cpp/.h
│   │       └── EventTimeline.cpp/.h
│   ├── simulation/
│   │   ├── ECSWorld.cpp/.h         # Wrapper EnTT
│   │   ├── TimeSystem.cpp/.h       # Tick + accélération temporelle
│   │   ├── EconomySystem.cpp/.h    # Production, échanges, prix
│   │   ├── PoliticsSystem.cpp/.h   # Gouvernement, lois, légitimité
│   │   ├── DiplomacySystem.cpp/.h  # Relations, traités, tensions
│   │   ├── CharacterSystem.cpp/.h  # IA personnages (ambitions, complots)
│   │   ├── MilitarySystem.cpp/.h   # Conflits, armées
│   │   └── EventSystem.cpp/.h      # Événements dynamiques (famines, révoltes)
│   └── main.cpp
├── shaders/
│   ├── planet.vert / planet.frag
│   ├── atmosphere.frag
│   ├── overlay.vert / overlay.frag
│   └── ui.vert / ui.frag
├── assets/
│   ├── textures/
│   └── fonts/
├── saves/
├── CMakeLists.txt
├── GamePlan.md
└── README.md
```

---

## Plan de réalisation

### Phase 0 — Fondations du moteur `[6-8 semaines]`
- Projet CMake + intégration de toutes les dépendances
- Window, contexte OpenGL, boucle de jeu
- Shader system (compilation GLSL, uniforms)
- Abstraction VAO/VBO/EBO
- Caméra orbitale (scroll zoom, drag rotation)
- **Livrable :** cube texturé qui tourne, texte à l'écran

### Phase 1 — Rendu planétaire `[6-8 semaines]`
- Icosphère subdivisée (meilleure topologie qu'une UV sphere)
- Génération de heightmap procédurale (bruit de Perlin 3D)
- LOD quadtree : subdivision dynamique selon distance caméra
- Shader atmosphère (Rayleigh scattering simplifié)
- Coloration biomes par latitude/altitude
- **Livrable :** planète 3D navigable de l'orbite au sol

### Phase 2 — Overlay 2D + UI `[4-5 semaines]`
- Projection coordonnées géographiques (lat/lon) → écran
- Rendu frontières et polygones territoriaux sur la sphère
- Instanced rendering pour les icônes carte (milliers en une draw call)
- Intégration Dear ImGui : panneaux contexte, filtres, timeline
- **Livrable :** carte politique superposée à la planète

### Phase 3 — Moteur de simulation `[8-10 semaines]`
- ECS avec EnTT (civilisations, provinces, personnages, ressources)
- Système de temps : ticks fixes, accélération x1/x5/x10/x100
- Économie : production → logistique → prix dynamiques
- Politique : régimes, lois, légitimité, stabilité
- Diplomatie : relations, traités, tensions
- Personnages : traits, ambitions, intrigues, réseaux d'influence
- **Livrable :** simulation jouable en console/tests unitaires

### Phase 4 — Connexion Simulation ↔ Rendu `[3-4 semaines]`
- Double buffer : thread simulation écrit A, thread rendu lit B, swap atomique
- Mapping état ECS → couleurs overlay (frontières, tensions, ressources)
- Événements → entrées timeline UI
- **Livrable :** monde simulé visible en temps réel sur la planète

### Phase 5 — MVP 0.1 jouable `[6-8 semaines]`
- 1 planète, 4 civilisations, ~25 provinces chacune
- 3 ressources (nourriture, métal, énergie)
- 5 personnages clés par civilisation
- Time control : pause, x1, x5
- Sauvegarde/chargement JSON
- Boucle de jeu de 20 minutes
- **Livrable :** prototype jouable et testable

### Durée totale estimée
**~9 à 11 mois** à ~4h/jour de travail sérieux.

---

## Composants ECS (exemples)

```cpp
struct Position       { float lat, lon; };
struct Civilization   { std::string name; uint32_t population; };
struct Resource       { ResourceType type; float quantity; float extractionRate; };
struct Territory      { std::vector<ProvinceId> provinces; };
struct Character      { std::string name; float ambition, loyalty, pragmatism; };
struct DiplomRelation { CivId target; float trust; TreatyFlags treaties; };
```

---

## Ressources d'apprentissage

- **OpenGL moderne :** [learnopengl.com](https://learnopengl.com) — faire l'intégralité des chapitres Getting Started → Advanced OpenGL
- **Architecture moteur :** The Cherno — série YouTube "Game Engine" (50+ épisodes, moteur C++ from scratch)
- **EnTT :** [github.com/skypjack/entt](https://github.com/skypjack/entt) — documentation officielle + exemples
- **Bruit de Perlin / terrain procédural :** [The Book of Shaders](https://thebookofshaders.com)

---

## Roadmap versions

| Version | Contenu |
|---|---|
| 0.1 Prototype | 1 planète, 4 civs, 3 ressources, boucle 20 min |
| 0.2 Alpha | Lois, clans/factions, intrigues, 2 ères jouables |
| 0.3 Beta | Équilibrage macro, optimisation late game, playtests |
| 1.0 Release | Multi-ères jusqu'au spatial, plusieurs planètes, sandbox + scénarios |

---

## Build

Prérequis : **CMake ≥ 3.20**, **Ninja**, un compilateur **C++20** (MinGW g++ 13+ ou MSVC), et **Python avec jinja2** (`pip install jinja2`) — utilisé pour générer le loader GLAD. Toutes les autres dépendances sont téléchargées automatiquement par CMake (FetchContent), aucune installation manuelle.

```powershell
# Build (configure automatiquement au premier lancement)
.\build.ps1

# Build + lancer
.\build.ps1 -Run

# Nettoyer
.\build.ps1 -Clean
```

Ou manuellement :

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\bin\WarLand.exe
```

> Note : si la génération de GLAD échoue avec `No module named 'jinja2'`, CMake utilise un Python sans jinja2. Passez le bon : `-DPython_EXECUTABLE=C:/chemin/vers/python.exe`. Le script `build.ps1` le détecte automatiquement.

## État actuel

### Phase 0 — Fondations ✅
- Fenêtre OpenGL 4.5 core + contexte (GLFW + GLAD)
- Système de shaders GLSL (compilation, link, uniforms) — [Shader](src/renderer/Shader.h)
- Caméra orbitale (drag = rotation, molette = zoom) — [Camera](src/renderer/Camera.h)
- Intégration Dear ImGui : HUD préfigurant le GamePlan (barre de temps pause / x1 / x2 / x5 / x10, panneau filtres, panneau contexte, timeline)

### Phase 1 — Rendu planétaire ✅
- **Icosphère subdivisée** (niveau 6 ≈ 80k triangles) — [PlanetMesh](src/renderer/planet/PlanetMesh.h)
- **Relief procédural** par bruit FBM/Perlin, océans aplatis au niveau de la mer
- **Coloration des biomes** par altitude (océan profond → côte → forêt → roche → neige) et latitude (calottes polaires) — [planet.frag](shaders/planet.frag)
- **Shader d'atmosphère** : halo additif autour du limbe, plus lumineux côté soleil — [atmosphere.frag](shaders/atmosphere.frag)
- Éclairage solaire directionnel avec cycle jour/nuit lent + spéculaire sur les océans

Contrôles : **drag souris** = orbite caméra · **molette** = zoom (jusqu'au sol).

## Prochaine étape — Phase 2

Overlay 2D sur la planète : projection lat/lon → écran, frontières et polygones de provinces sur la sphère, rendu instancié des icônes de carte, couches activables via le panneau Filtres. Voir [renderer/overlay/](src/renderer/) dans la structure ci-dessus.
