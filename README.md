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
- **Nuages volumétriques** : raymarching d'un bruit 3D (FBM) dans une coquille sphérique, auto-ombrage par le soleil, **LOD** (18/32/48 pas selon la distance) et **fondu au zoom** (les nuages s'effacent près du sol) — [clouds.frag](shaders/clouds.frag)
- Éclairage solaire directionnel avec cycle jour/nuit lent + spéculaire sur les océans

### Cycle de l'eau, météo & saisons ✅
- **Simulation du cycle de l'eau** sur une grille lat/lon (128×64) — [Climate](src/simulation/Climate.h) : **évaporation** des océans chauds → **transport par les vents zonaux** (alizés / vents d'ouest) → **condensation/précipitation** avec **effet orographique** (montagnes au vent humides, sous le vent sèches) et **ceintures subtropicales sèches** (~30°)
- **De vrais déserts persistants** émergent (intérieurs continentaux, ombres pluviométriques, zones subtropicales) — pluie variant de 0 (désert) à 1 (zone humide) sur la planète
- **Nuages pilotés par le climat** : la couverture nuageuse simulée est envoyée au shader via une **texture** → les nuages **tournent avec la planète** et **évoluent réellement** (plus de pulsation), s'assombrissant là où ils sont épais — [clouds.frag](shaders/clouds.frag)
- **Connexion ciel ↔ sol** : la pluie au-dessus d'une province **pilote sa production de nourriture** → la population se concentre dans les régions fertiles, les déserts restent peu peuplés
- **Saisons** : la latitude du soleil oscille sur l'année → production réduite en hiver et aux hautes latitudes
- **Événements météo** : **sécheresses** (zones sèches), **inondations** (zones très pluvieuses)
- HUD : **saison** dans la barre, **météo (%)** de la province dans le Contexte

### Phase 2 — Overlay politique ✅ (en cours)
- **Découpage en provinces** par Voronoï sphérique (graines réparties via spirale de Fibonacci) — [ProvinceMap](src/renderer/overlay/ProvinceMap.h)
- **Regroupement en civilisations** : chaque province appartient à la capitale la plus proche → nations contiguës, couleur distincte par civ (roue chromatique HSV)
- **Couche politique** rendue en overlay translucide posé sur le terrain, avec facettes nettes (`flat` shading) — [overlay.frag](shaders/overlay.frag)
- Activable via la case **Politique** du panneau Filtres ; le panneau Contexte affiche le nombre de provinces et de civilisations

Contrôles : **drag souris** = orbite caméra · **molette** = zoom (jusqu'au sol) · case **Politique** = afficher/masquer les nations · **clic gauche** sur une province = la sélectionner.

### Phase 3 — Simulation vivante ✅
- **ECS EnTT** : une entité par province avec composants `CProvince`, `CPopulation`, `CStock` — [Components](src/simulation/Components.h), [SimulationWorld](src/simulation/SimulationWorld.h)
- **Biomes** dérivés de l'élévation + latitude moyennes de chaque province (océan, désert, prairie, forêt, toundra, montagne, polaire)
- **TimeSystem** branché sur la barre x1/x5/x10 : `SimClock::advance()` renvoie les jours in-game, `SimulationWorld::tick()` les applique
- **Dynamique de population pilotée par la nourriture** : production `= rendement_biome × √pop` (→ capacité de charge naturelle), consommation `= pop × besoin`. Surplus → croissance, stock épuisé → **famine** et déclin
- **3 ressources vivantes** : nourriture (moteur démographique), matériaux et énergie (extraction vs entretien)
- **Heatmap de population** : couche **Population** recolorant le globe (bleu = peu peuplé → rouge = dense), rafraîchie en continu
- Panneau **Contexte** : biome, population, état alimentaire et stocks **live** de la province sélectionnée ; barre supérieure : **stabilité** et **population mondiale** réelles

La simulation est **découplée du rendu** ([SimulationWorld](src/simulation/SimulationWorld.cpp) ne touche jamais à OpenGL).

### Phase 3b — Événements & chocs ✅
- **Sécheresse** (production de nourriture à 35 %, 1–3 ans), **épidémie** (mortalité continue), **récolte exceptionnelle** (bonus) — composant `CAffliction`, tirage ~Poisson
- Les chocs créent de **vraies famines** → la stabilité chute, la heatmap se vide localement
- **Journal historique** horodaté → la **timeline** du bas se remplit (coloré par gravité)

### Phase 4 — Migration & commerce ✅
- **Graphe d'adjacence** des provinces construit depuis les arêtes du maillage (deux provinces voisines si une arête de triangle les relie) — [ProvinceMap::neighbors](src/renderer/overlay/ProvinceMap.h) (~6 voisins/province)
- **Commerce** : la nourriture diffuse des provinces riches vers les voisines moins pourvues → les routes commerciales atténuent les famines locales
- **Migration** : les populations affamées **fuient** vers les voisins en surplus, réparties au prorata de leur situation alimentaire
- Échanges calculés en **deltas** puis appliqués (pas de dépendance à l'ordre d'itération) — [SimulationWorld::exchangeBetweenProvinces](src/simulation/SimulationWorld.cpp)

### Phase 5 — Simulation multi-thread ✅
- La simulation tourne sur **son propre thread** ([SimulationRunner](src/simulation/SimulationRunner.h)), totalement séparée du rendu
- Le thread sim avance le temps en **temps réel** (vitesse/pause via atomiques) et publie un **Snapshot** sous mutex ; le rendu en lit une copie à chaque frame
- **60 FPS garantis** même si la simulation devient lourde ; l'accélération du temps (x10) ne bloque jamais l'affichage
- Pas de data race : le rendu ne touche jamais le `registry` ECS, il lit uniquement le snapshot

### Phase 6 — Sauvegarde / Chargement ✅
- État vivant sérialisé en **JSON** (nlohmann/json) : population, stocks, afflictions, événements, année — [SimulationWorld::saveToFile](src/simulation/SimulationWorld.cpp)
- Rejoué sur les entités existantes (topologie déterministe via la seed)
- I/O exécutées sur le **thread de simulation** (`requestSave`/`requestLoad`), boutons **Sauver / Charger** dans le HUD

### Phase 7 — Diplomatie & frontières ✅
- **Frontières nettes** : lignes sombres rendues sur les arêtes du maillage où deux civilisations se touchent — [ProvinceMap::drawBorders](src/renderer/overlay/ProvinceMap.h)
- **Diplomatie** : matrice d'opinion civ × civ (-100 guerre → +100 alliance), marche aléatoire + chocs diplomatiques + retour à la neutralité — [SimulationWorld::tickDiplomacy](src/simulation/SimulationWorld.cpp)
- **Guerres/paix** déclenchées aux seuils, loguées dans la timeline
- Panneau **Diplomatie** : population par civilisation + matrice de relations colorée (vert = allié, rouge = guerre)

### Phase 8 — Ères & conséquences des guerres ✅
- **Progression d'ères** : chaque civilisation accumule des points de technologie (selon sa population) et franchit les ères **Âge de pierre → Antiquité → Industrielle → Numérique → Spatiale** — [SimulationWorld::tickTech](src/simulation/SimulationWorld.cpp)
- L'**ère globale** (civilisation la plus avancée) s'affiche dans la barre du haut ; l'ère par civ dans le panneau Diplomatie
- **Embargo de guerre** : deux civilisations en guerre **cessent tout commerce** et leurs populations ne se réfugient plus chez l'ennemi → les conflits deviennent économiquement coûteux (effet émergent sur les famines)

### Phase 9 — Expansion dynamique des civilisations ✅
- **Appartenance dynamique** : chaque civilisation démarre avec **une seule province** (sa capitale) ; tout le reste est **terre sauvage** (`civ = -1`) — [SimulationWorld::tickExpansion](src/simulation/SimulationWorld.cpp)
- **Colonisation** : les civs s'étendent dans les terres sauvages voisines selon leur **force projetée** (√population × techno × emprise), avec **usure impériale** (plus une civ est grande, plus elle s'étend lentement) → des tailles d'empire variées et émergentes
- **Conquête** : en guerre, l'emprise (`control` 0–100) d'une province frontalière s'érode sous la pression ennemie ; à 0 elle **change de propriétaire** (loguée dans la timeline)
- **Frontières mobiles** : l'overlay politique et les lignes de frontière sont **recolorés/reconstruits en continu** depuis l'état simulé ([ProvinceMap::rebuildBorders](src/renderer/overlay/ProvinceMap.h))
- Échelle de temps accélérée (40 j/s à x1) pour voir les empires croître ; panneau Diplomatie : population, **nb de provinces** et ère par civ
- *Validé : de 7 provinces (capitales) à 58/97 colonisées en ~12 ans simulés*

### Phase 10 — Cohésion & révoltes ✅
- **Expansion ralentie et proportionnelle** : constantes de colonisation/conquête réduites (~2,5×) et **usure impériale renforcée** (`1/(1+0.08·n)`) → les avancées sont réalistes par rapport à la taille de l'empire
- **Instabilité** : chaque province subit une pression de révolte croissant avec la **taille de l'empire** (`0.05·nbProvinces/jour`) et la **famine** ; si l'emprise tombe à 0 sans conquérant, la province **se révolte et redevient sauvage** (loguée dans la timeline)
- Résultat : les empires **croissent puis plafonnent**, perdent leurs provinces périphériques mal tenues, et le monde garde des terres sauvages — *validé : ~27/97 colonisées à 10 ans, ~56/97 à 32 ans, tailles variées 3–14*

### Phase 11 — Cohérence : la population suit les États ✅
- **La population n'existe que dans les provinces possédées** : les terres sauvages sont **vides** (0 habitant, 0 stock, non simulées) — plus de population « hors-sol »
- **Colonisation = installation de colons** : prendre une terre sauvage y **transfère des colons** prélevés sur les provinces voisines du colonisateur (+ un noyau de base) → la population se *déplace*, elle n'apparaît pas par magie
- **Conquête** : la population reste sur place et change de souverain
- **Révolte = exode** : la population **fuit vers les provinces voisines de l'empire** (réfugiés) et la province se vide
- **Commerce, migration, événements, stabilité** ne concernent plus que les provinces habitées (aucun échange avec le vide)
- *Validé : 75 provinces possédées toutes peuplées, 326 terres sauvages à population nulle*

### Sécessions & villes ✅
- **Révoltes = indépendance** : une province révoltée assez peuplée **proclame une nouvelle civilisation** (`SimulationWorld::secede`), en **guerre contre son ancien maître** ; les matrices civ×civ (opinions, guerres) s'agrandissent dynamiquement (jusqu'à 40 civs). Des sécessions en chaîne sont possibles (une civ née d'une révolte peut elle-même se fragmenter)
- **Villes** : chaque province possédée porte une **ville** rendue par un **point** sur le globe, dont la taille suit la population (Village → Bourg → Ville → Métropole) — [city.frag](shaders/city.frag) ; stats affichées dans le panneau Contexte
- **Routes** : réseau de **lignes** reliant les villes voisines d'une même civilisation
- Couleurs de civilisation désormais **procédurales** (teinte par nombre d'or) → valables pour les civs nées dynamiquement

### Cultures & langues ✅
- Chaque province a une **culture** (langue) ; les **colons propagent la leur** sur les terres vierges, mais les provinces **conquises gardent leur culture** → minorités sous domination étrangère
- **Instabilité culturelle** : une province de culture différente de son propriétaire est **plus rebelle** → les empires multiculturels se **fragmentent selon les lignes culturelles** (révoltes/sécessions), chaque sécession fondant une nation de sa propre langue
- **Barrière de la langue en diplomatie** : les civilisations de **même culture se rapprochent**, les cultures différentes **s'éloignent** (biais sur l'opinion)
- **Couche « Langue »** : colore le globe par culture (palette dédiée) + **frontières culturelles** faisant ressortir les minorités — panneau Contexte indique la culture et l'étiquette *(minorité)*

### Villes, commerce culturel & multi-planètes ✅
- **Urbanisation** : la population urbaine est une fraction croissant avec l'ère (âge de pierre ~4 % → ère spatiale ~60 %) ; taille du point et niveau (Village/Bourg/Ville/Métropole) basés sur l'urbain
- **Spécialisation** des villes : Portuaire (côtier), Minière (montagne), Industrielle, Agricole, Rurale
- **Commerce freiné par la langue** : les échanges entre provinces de cultures différentes sont réduits (barrière culturelle)
- **Multi-planètes** : un **système de 3 mondes** générés différemment, **simulés en parallèle** (chacun sa géographie, ses civilisations, son climat) — [SimulationRunner](src/simulation/SimulationRunner.h) gère N `SimulationWorld`. Un **sélecteur « Monde k/N »** permet de voyager entre les planètes ; le rendu n'affiche que la planète active (ressources de rendu partagées) → coût GPU maîtrisé

### Échelles de temps réelles + curseur ✅
- La vitesse n'est plus un multiplicateur abstrait mais des **échelles concrètes** (1 seconde réelle = …) : **Réel** (1 s), **Heure**, **Jour**, **Mois**, **Année**
- **Curseur** par échelle : le multiplicateur se règle finement (ex. 1→24 heures/s, 1→30 jours/s) → pas seulement « heure par heure » mais « 5 heures par seconde » etc.
- « Réel » prépare l'observation des **batailles** ; « Heure/Jour » pour le détail ; « Mois/Année » pour le long terme
- **Rotation de la planète, jour/nuit et évolution des nuages calés sur le temps in-game** : tout ralentit en mode lent (figé en Réel) et accélère en mode rapide → la météo « vit » à la vitesse choisie

### Vue système & voyage interplanétaire ✅
- **Vue orbitale** : une étoile centrale + les **planètes sur leurs orbites animées** (anneaux), chaque planète éclairée par l'étoile — bouton **« Vue système »** dans la barre du haut
- **Voyage** : un **clic sur une planète** dans la vue système zoome dessus (elle devient la planète active) ; retour à la vue système quand on veut
- **Colonisation interplanétaire** : une civilisation parvenue à l'**ère numérique/spatiale** essaime vers un autre monde → fonde une **nouvelle civilisation coloniale** (de sa culture) sur une terre vierge, loguée dans la timeline

## Prochaine étape

- **Lois & régimes politiques** évolutifs (cf. GamePlan).
- **Religions** et leur diffusion.
- **Espionnage & intrigues** des personnages clés — cf. [GamePlan.md](GamePlan.md).
