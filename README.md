# Warland

Prototype C++20 d’un jeu de stratégie à 3 niveaux (Monde/Ville/Personnage).

Démarrage rapide
- Prérequis: CMake 3.20+, Visual Studio Build Tools (MSVC), PowerShell
- Configurer + build: Terminal > Exécuter la tâche "build" (ou scripts/build.ps1 -Configure)
- Lancer: Tâche "run" ou F5 (Run Warland)

Docs
- docs/TECH_STACK.md – pile technique proposée
- docs/ARCHITECTURE.md – architecture haut niveau
- docs/DESIGN.md – gameplay/UX
- docs/ROADMAP.md – jalons

Structure
- src/ – moteur + gameplay (ECS, rendu, physique, IA, UI, etc.)
- assets/ – contenus
- data/ – définitions JSON, localisation, configs
- scripts/ – build/run/setup
- tests/ – futurs tests
