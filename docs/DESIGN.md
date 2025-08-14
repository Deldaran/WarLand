# Design Gameplay & UX

Vision
- Stratégie de pays avec twist: exécution des ordres via messagers physiques
- 3 niveaux cohérents (Monde -> Ville -> Personnage) avec distances réelles

Niveau Monde (2D top)
- Carte avec frontières, villes (points), routes
- Envoi d’ordres: créer missives, assigner coursiers, délais dépendant des routes/fog
- Diplomatie macro, économie agrégée, brouillard de guerre
- UI: MapUI, overlays routes/délais, files d’ordres

Niveau Ville (3D isométrique)
- Bâtiments visitables, citoyens simulés (jobs, besoins)
- Réseaux (eau, élec), trafic, logistique
- Construction/améliorations, placement routes urbaines
- UI: CityUI, panneaux bâtiment, heatmaps utilitaires/trafic

Niveau Personnage (3D iso)
- Contrôle clique-déplacement, interactions contextuelles
- Inventaire, métiers, relations, besoins
- Caméra personnage, animations, comportement local (suivi des ordres)

Systèmes transverses
- LOD simulation: monde (agrégé) / ville (détaillé) / perso (fin)
- Messaging in-universe vs EventBus moteur
- Temps: calendrier, timewarp, pauses lors de transitions

Contrôles par défaut (PC)
- Déplacement caméra: WASD + souris; zoom molette
- Sélection: clic gauche; actions: clic droit; UI: ESC/Enter

Accessibilité
- Remap des touches, daltonisme (palettes), vitesses de jeu
