# Architecture

Objectifs
- 3 niveaux jouables partageant les mêmes entités/données (mètres): Monde (2D top), Ville (3D iso), Personnage (3D iso intérieur/ext.)
- Simulation déterministe à pas fixe; rendu indépendant
- Extensibilité via ECS, data-driven (JSON + scripts Lua)

Couches
1) Platform
   - Fenêtre, Input, FS, horloge, threads
2) Core
   - Application (boucle), Time, Log, EventBus, Config
3) Resources
   - AssetLoader, ResourceManager, hot-reload
4) Engine
   - ECS (Entity, Registry, Components, Systems)
   - Rendering (Renderer, Camera, Mesh, Texture, Shader, SceneGraph, Terrain)
   - Physics (Physics, Collision, NavMesh)
   - AI (BT, GOAP, Pathfinding)
   - Simulation (Scheduler, LOD, SaveGame, Timewarp)
   - Audio (AudioEngine)
   - Scripting (pont Lua)
5) Gameplay
   - Common (Factions, Diplomacy, Economy, Messaging, Calendar)
   - WorldMap (WorldMapController, Province, Country, RoadNetwork, Courier, FogOfWar, MapGenerator)
   - City (CityController, Building, Citizen, JobSystem, UtilityNetworks, TrafficSystem, CityGenerator)
   - Character (CharacterController, Inventory, Interaction, Animation, CharacterCamera)
6) UI
   - HUD, Menus, Overlays, MapUI, CityUI, CharacterUI + ImGuiLayer
7) Tools
   - MapEditorApp, CityEditorApp, NavmeshBuilderApp, AssetPackerApp

Flux de la boucle principale
- Input -> Simulation (pas fixe) -> Events -> Rendu (delta)
- Scheduler orchestre les systèmes actifs selon le niveau courant
- Streaming: chargement/ déchargement d’entités par proximité/caméra/LOD

Changement de niveaux
- WorldMapController, CityController, CharacterController sont des "game states"
- Transition:
  - Figer la sim, sérialiser sous-ensemble si besoin
  - Activer/désactiver systèmes (ex: Traffic/Utility en ville, GOAP détaillé en perso)
  - Mettre à jour caméra et UI correspondantes

Messaging in-universe
- Les ordres sont encapsulés en messages, transportés par `Courier` via `RoadNetwork` avec délais réels
- Décorréler des événements moteur (bus d’événements interne)

Données et unités
- Coordonnées en mètres
- Monde: Echelle carte -> mètres; Ville: 1u = 1m; Intérieur: même unité

Sauvegarde/chargement
- JSON compressé, versionné, backward-compat

Tests
- Unitaires sur systèmes ECS, data validation; Intégration headless pour la simulation
