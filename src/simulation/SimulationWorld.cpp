#include "simulation/SimulationWorld.h"
#include "renderer/overlay/ProvinceMap.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

namespace wl {

namespace {

// Rendements par biome : nourriture / materiaux / energie.
// La production passe par sqrt(population) -> capacite de charge naturelle.
const BiomeYield kYields[] = {
    /* Ocean     */ {0.00, 0.00, 0.00},
    /* Desert    */ {0.30, 0.45, 0.95},
    /* Grassland */ {1.20, 0.40, 0.45},
    /* Forest    */ {0.90, 0.75, 0.35},
    /* Tundra    */ {0.45, 0.55, 0.35},
    /* Mountain  */ {0.35, 1.20, 0.55},
    /* Polar     */ {0.18, 0.30, 0.25},
};

const char* kBiomeNames[] = {
    "Ocean", "Desert", "Prairie", "Foret", "Toundra", "Montagne", "Polaire"};

const char* kEventNames[] = {"Secheresse", "Epidemie", "Recolte exceptionnelle"};

// Eres : seuils cumulatifs de points de technologie.
const char* kEraNames[] = {"Age de pierre", "Antiquite", "Ere industrielle",
                           "Ere numerique", "Ere spatiale"};
const double kEraThresholds[] = {0.0, 150.0, 600.0, 1800.0, 5000.0};

// Probabilite globale qu'un choc survienne, par jour in-game.
// ~ un evenement tous les 1.5 ans environ a l'echelle du monde.
constexpr double kEventGlobalRatePerDay = 1.0 / (1.5 * 365.0);

// Consommation par habitant (par jour).
constexpr double kFoodPerCapita = 0.010;
constexpr double kMaterialsPerCapita = 0.002;
constexpr double kEnergyPerCapita = 0.003;

// Facteurs de production (multiplient yield * sqrt(pop)).
constexpr double kFoodProdFactor = 2.0;
constexpr double kMaterialsProdFactor = 1.2;
constexpr double kEnergyProdFactor = 1.2;

Biome deriveBiome(float elevation, float latitude, float seaLevel) {
    if (elevation < seaLevel) return Biome::Ocean;
    float n = (elevation - seaLevel) / std::max(0.001f, 1.0f - seaLevel); // 0..1
    if (latitude > 0.82f) return Biome::Polar;
    if (n > 0.55f)        return Biome::Mountain;
    if (latitude > 0.60f) return Biome::Tundra;
    if (n < 0.08f)        return Biome::Desert;
    if (n < 0.30f)        return Biome::Grassland;
    return Biome::Forest;
}

} // namespace

const char* SimulationWorld::biomeName(Biome b) {
    int i = static_cast<int>(b);
    if (i < 0 || i >= static_cast<int>(Biome::Count)) return "?";
    return kBiomeNames[i];
}

const char* SimulationWorld::eventName(EventType t) {
    int i = static_cast<int>(t);
    if (i < 0 || i >= static_cast<int>(EventType::Count)) return "?";
    return kEventNames[i];
}

const char* SimulationWorld::eraName(int era) {
    int n = static_cast<int>(sizeof(kEraNames) / sizeof(kEraNames[0]));
    if (era < 0) era = 0;
    if (era >= n) era = n - 1;
    return kEraNames[era];
}

int SimulationWorld::eraForTech(double tech) {
    int n = static_cast<int>(sizeof(kEraThresholds) / sizeof(kEraThresholds[0]));
    int era = 0;
    for (int i = 1; i < n; ++i) {
        if (tech >= kEraThresholds[i]) era = i;
    }
    return era;
}

void SimulationWorld::init(const ProvinceMap& provinces, float seaLevel) {
    m_registry.clear();
    m_events.clear();
    m_inhabited.clear();
    const int n = provinces.provinceCount();
    m_byProvince.assign(n, entt::null);

    // Copie du graphe d'adjacence pour le commerce / la migration.
    m_neighbors.assign(n, {});
    for (int p = 0; p < n; ++p) m_neighbors[p] = provinces.neighbors(p);

    // Diplomatie : opinions inter-civilisations initialisees autour de 0.
    m_civCount = provinces.civCount();
    m_opinion.assign(m_civCount * m_civCount, 0.0f);
    m_warState.assign(m_civCount * m_civCount, 0);
    m_civPopulation.assign(m_civCount, 0.0);
    m_civTech.assign(m_civCount, 0.0);
    m_civProvinceCount.assign(m_civCount, 0);
    std::uniform_real_distribution<float> initOp(-20.0f, 20.0f);
    for (int a = 0; a < m_civCount; ++a) {
        for (int b = a + 1; b < m_civCount; ++b) {
            float v = initOp(m_rng);
            m_opinion[a * m_civCount + b] = v;
            m_opinion[b * m_civCount + a] = v;
        }
    }

    auto capacityOf = [](Biome b) {
        const BiomeYield& y = kYields[static_cast<int>(b)];
        return std::pow(100.0 * y.food * kFoodProdFactor, 2.0);
    };

    // Toutes les terres demarrent SAUVAGES (civ = -1) avec une petite
    // population native ; seules les capitales appartiennent a une civ.
    for (int p = 0; p < n; ++p) {
        entt::entity e = m_registry.create();
        m_byProvince[p] = e;

        float elev = provinces.provinceElevation(p);
        float lat = provinces.provinceLatitude(p);
        Biome biome = deriveBiome(elev, lat, seaLevel);
        bool ocean = (biome == Biome::Ocean);

        m_registry.emplace<CProvince>(e, CProvince{p, -1, biome, ocean, 0.0, -1});

        double pop = ocean ? 0.0 : std::max(150.0, capacityOf(biome) * 0.04);
        m_registry.emplace<CPopulation>(e, CPopulation{pop, 0.0});
        m_registry.emplace<CStock>(e, CStock{ocean ? 0.0 : 50.0,
                                             ocean ? 0.0 : 20.0,
                                             ocean ? 0.0 : 20.0});
        if (!ocean) m_inhabited.push_back(e);
    }

    // Une capitale par civilisation : la meilleure province (nourriture) de sa
    // region d'origine. C'est le point de depart de l'expansion.
    for (int c = 0; c < m_civCount; ++c) {
        int best = -1;
        double bestCap = -1.0;
        for (int p = 0; p < n; ++p) {
            if (provinces.provinceCiv(p) != c) continue;
            entt::entity e = m_byProvince[p];
            const CProvince& pr = m_registry.get<CProvince>(e);
            if (pr.ocean) continue;
            double cap = capacityOf(pr.biome);
            if (cap > bestCap) { bestCap = cap; best = p; }
        }
        if (best >= 0) {
            entt::entity e = m_byProvince[best];
            CProvince& pr = m_registry.get<CProvince>(e);
            pr.civ = c;
            pr.control = 100.0;
            m_registry.get<CPopulation>(e).count = capacityOf(pr.biome) * 0.30;
        }
    }

    recomputeAggregates();

    size_t edges = 0;
    for (auto& nb : m_neighbors) edges += nb.size();
    double deg = n > 0 ? static_cast<double>(edges) / n : 0.0;
    std::cerr << "[Sim] " << n << " provinces, " << m_inhabited.size()
              << " habitees, degre moyen " << deg << "\n";
}

void SimulationWorld::tick(double days, int year) {
    if (days <= 0.0) return;
    days = std::min(days, 10.0); // garde-fou contre les gros pas de temps

    auto view = m_registry.view<CProvince, CPopulation, CStock>();
    for (auto e : view) {
        const CProvince& prov = view.get<CProvince>(e);
        if (prov.ocean) continue;

        CPopulation& pop = view.get<CPopulation>(e);
        CStock& stock = view.get<CStock>(e);
        const BiomeYield& y = kYields[static_cast<int>(prov.biome)];

        // Choc actif sur la province (secheresse, epidemie...).
        double foodFactor = 1.0;
        double mortalityPerDay = 0.0;
        if (CAffliction* aff = m_registry.try_get<CAffliction>(e)) {
            foodFactor = aff->foodFactor;
            mortalityPerDay = aff->mortalityPerDay;
            aff->daysLeft -= days;
            if (aff->daysLeft <= 0.0) {
                logEvent(year, std::string("Fin de ") + eventName(aff->type)
                         + " - province #" + std::to_string(prov.id), 0);
                m_registry.remove<CAffliction>(e);
            }
        }

        double workers = std::max(0.0, pop.count);
        double sq = std::sqrt(workers);

        // --- Nourriture : moteur de la dynamique de population ---
        double foodProd = y.food * sq * kFoodProdFactor * foodFactor;
        double foodCons = workers * kFoodPerCapita;
        double balance = foodProd - foodCons; // par jour
        pop.lastFoodBalance = balance;
        stock.food += balance * days;

        if (stock.food >= 0.0) {
            // Surplus -> croissance proportionnelle au ratio de surplus.
            double ratio = balance / (foodCons + 1.0);
            double growthPerDay = 0.0005 * std::clamp(ratio, 0.0, 1.0);
            pop.count = workers * std::pow(1.0 + growthPerDay, days);
            // On ne garde qu'une partie du surplus en stock (gaspillage/peremption).
            stock.food = std::min(stock.food, foodCons * 120.0);
        } else {
            // Penurie : le stock est vide, la famine reduit la population.
            double deficit = -stock.food;
            stock.food = 0.0;
            double deathPerDay = std::min(0.004, 0.004 * deficit / (foodCons * days + 1.0));
            pop.count = workers * std::pow(1.0 - deathPerDay, days);
        }

        // Mortalite supplementaire due a un choc (epidemie).
        if (mortalityPerDay > 0.0) {
            pop.count *= std::pow(1.0 - mortalityPerDay, days);
        }
        pop.count = std::clamp(pop.count, 0.0, 5.0e6);

        // --- Materiaux & energie : extraction vs entretien ---
        double newSq = std::sqrt(std::max(0.0, pop.count));
        stock.materials += (y.materials * newSq * kMaterialsProdFactor
                            - pop.count * kMaterialsPerCapita) * days;
        stock.energy += (y.energy * newSq * kEnergyProdFactor
                         - pop.count * kEnergyPerCapita) * days;
        stock.materials = std::clamp(stock.materials, 0.0, 1.0e6);
        stock.energy = std::clamp(stock.energy, 0.0, 1.0e6);
    }

    exchangeBetweenProvinces(days);
    tickDiplomacy(days, year);
    spawnEvents(days, year);
    recomputeAggregates();      // populations / comptes / par civ
    tickTech(days);             // techno (depend des populations par civ)
    tickExpansion(days, year);  // colonisation + conquete (depend tech/comptes)
    recomputeAggregates();      // rafraichit les comptes apres changements d'appartenance
}

void SimulationWorld::tickTech(double days) {
    // La recherche progresse avec la population (main-d'oeuvre savante).
    for (int c = 0; c < m_civCount; ++c) {
        m_civTech[c] += m_civPopulation[c] * 1.0e-6 * days;
    }
}

void SimulationWorld::tickExpansion(double days, int year) {
    if (m_civCount < 1) return;
    const size_t n = m_byProvince.size();

    // Constantes d'expansion (calibrees pour une expansion lente et graduelle,
    // proportionnelle a la force reelle et freinee par l'usure imperiale).
    constexpr double kBaseRegen = 0.5;    // regain d'emprise du proprietaire / jour
    constexpr double kFriendly  = 0.0015; // soutien des provinces alliees voisines
    constexpr double kHostile   = 0.0004; // erosion par les ennemis (guerre)
    constexpr double kColonize  = 0.00030; // progression de la colonisation
    // Cohesion (Phase 10) : un empire trop grand ou affame se desagrege.
    constexpr double kUnrestPerProv = 0.05; // instabilite par province possedee / jour
    constexpr double kFamineUnrest  = 0.6;  // une famine attise la revolte

    // Passe 1 : force projetee par chaque province (population, techno, usure).
    // L'usure imperiale (1/(1+0.08*n)) penalise fortement les grands empires :
    // les avancees restent proportionnelles a la taille reelle.
    std::vector<double> strength(n, 0.0);
    for (size_t p = 0; p < n; ++p) {
        entt::entity e = m_byProvince[p];
        if (e == entt::null) continue;
        const CProvince& pr = m_registry.get<CProvince>(e);
        if (pr.ocean || pr.civ < 0) continue;
        double pop = m_registry.get<CPopulation>(e).count;
        double tech = 1.0 + 0.25 * eraForTech(m_civTech[pr.civ]);
        double overstretch = 1.0 / (1.0 + 0.08 * m_civProvinceCount[pr.civ]); // usure imperiale
        strength[p] = std::sqrt(std::max(0.0, pop)) * tech * overstretch * (pr.control / 100.0);
    }

    // Passe 2 : calcul des changements d'emprise / d'appartenance.
    // flip : 0 = aucun, 1 = conquete, 2 = revolte (retour a l'etat sauvage).
    struct Change { int owner; double control; int contender; int flip; int oldOwner; };
    std::vector<Change> chg(n);
    std::vector<double> civStr(m_civCount, 0.0);

    for (size_t p = 0; p < n; ++p) {
        entt::entity e = m_byProvince[p];
        if (e == entt::null) { chg[p] = {-1, 0.0, -1, 0, -1}; continue; }
        const CProvince& pr = m_registry.get<CProvince>(e);
        if (pr.ocean) { chg[p] = {pr.civ, pr.control, pr.contender, 0, pr.civ}; continue; }

        int ownerN = pr.civ;
        double controlN = pr.control;
        double foodBalanceN = m_registry.get<CPopulation>(e).lastFoodBalance;

        // Force de chaque civ voisine.
        std::fill(civStr.begin(), civStr.end(), 0.0);
        for (int q : m_neighbors[p]) {
            entt::entity eq = m_byProvince[q];
            if (eq == entt::null) continue;
            const CProvince& pq = m_registry.get<CProvince>(eq);
            if (pq.ocean || pq.civ < 0) continue;
            civStr[pq.civ] += strength[q];
        }

        double friendly = (ownerN >= 0) ? civStr[ownerN] : 0.0;
        int contender = -1;
        double best = 0.0;
        for (int c = 0; c < m_civCount; ++c) {
            if (c == ownerN || civStr[c] <= 0.0) continue;
            bool allowed = (ownerN < 0) ? true : atWar(ownerN, c); // sauvage: colonisable ; sinon guerre
            if (allowed && civStr[c] > best) { best = civStr[c]; contender = c; }
        }
        double hostile = best;

        Change ch{ownerN, controlN, contender, 0, ownerN};
        if (ownerN >= 0) {
            // Instabilite : croit avec la taille de l'empire et la famine.
            double unrest = kUnrestPerProv * m_civProvinceCount[ownerN];
            if (foodBalanceN < 0.0) unrest += kFamineUnrest;

            double dc = (kBaseRegen + friendly * kFriendly
                         - hostile * kHostile - unrest) * days;
            ch.control = std::clamp(controlN + dc, 0.0, 100.0);
            if (ch.control <= 0.0) {
                if (contender >= 0) {
                    ch.owner = contender;     // province conquise
                    ch.control = 15.0;
                    ch.flip = 1;
                } else {
                    ch.owner = -1;            // revolte -> redevient sauvage
                    ch.control = 0.0;
                    ch.contender = -1;
                    ch.flip = 2;
                }
            }
        } else {
            if (contender >= 0) {
                if (pr.contender != contender) controlN *= 0.3; // un nouveau colonisateur repart presque de zero
                ch.control = controlN + hostile * kColonize * days;
                if (ch.control >= 100.0) { ch.owner = contender; ch.control = 60.0; } // colonisee
            } else {
                ch.control = std::max(0.0, controlN - 2.0 * days);
            }
        }
        chg[p] = ch;
    }

    // Application + journalisation des conquetes et revoltes (les colonisations
    // sont trop frequentes pour etre toutes loguees).
    for (size_t p = 0; p < n; ++p) {
        entt::entity e = m_byProvince[p];
        if (e == entt::null) continue;
        CProvince& pr = m_registry.get<CProvince>(e);
        if (pr.ocean) continue;
        if (chg[p].flip == 1) {
            logEvent(year, "Conquete : civ " + std::to_string(chg[p].owner)
                     + " prend la province #" + std::to_string(pr.id)
                     + " a civ " + std::to_string(chg[p].oldOwner), 2);
        } else if (chg[p].flip == 2) {
            logEvent(year, "Revolte : la province #" + std::to_string(pr.id)
                     + " se separe de civ " + std::to_string(chg[p].oldOwner), 1);
        }
        pr.civ = chg[p].owner;
        pr.control = chg[p].control;
        pr.contender = chg[p].contender;
    }
}

void SimulationWorld::tickDiplomacy(double days, int year) {
    if (m_civCount < 2) return;

    std::normal_distribution<double> noise(0.0, 1.0);
    std::uniform_real_distribution<double> u01(0.0, 1.0);

    for (int a = 0; a < m_civCount; ++a) {
        for (int b = a + 1; b < m_civCount; ++b) {
            int idx = a * m_civCount + b;
            float v = m_opinion[idx];

            // Marche aleatoire + leger retour vers la neutralite.
            double d = noise(m_rng) * 0.4 * days - 0.01 * v * days;
            // Choc diplomatique occasionnel (incident ou traite).
            if (u01(m_rng) < 0.0008 * days) d += (u01(m_rng) < 0.5 ? -30.0 : 30.0);

            v = std::clamp(v + static_cast<float>(d), -100.0f, 100.0f);
            m_opinion[idx] = v;
            m_opinion[b * m_civCount + a] = v;

            // Transitions guerre / paix.
            bool warNow = v <= -60.0f;
            bool warBefore = m_warState[idx] != 0;
            if (warNow != warBefore) {
                m_warState[idx] = m_warState[b * m_civCount + a] = warNow ? 1 : 0;
                logEvent(year,
                    (warNow ? "Guerre declaree : civ " : "Paix signee : civ ")
                        + std::to_string(a) + (warNow ? " vs civ " : " et civ ")
                        + std::to_string(b),
                    warNow ? 2 : 0);
            }
        }
    }
}

void SimulationWorld::exchangeBetweenProvinces(double days) {
    const size_t n = m_byProvince.size();
    if (n == 0) return;

    std::vector<double> foodDelta(n, 0.0);
    std::vector<double> popDelta(n, 0.0);

    // Fraction des ecarts effectivement transferee sur ce pas de temps.
    double tradeFrac = std::clamp(0.05 * days, 0.0, 0.40);   // commerce de nourriture
    double migFrac = std::clamp(0.004 * days, 0.0, 0.40);    // depart des affames

    for (size_t pid = 0; pid < n; ++pid) {
        entt::entity e = m_byProvince[pid];
        if (e == entt::null) continue;
        const CProvince& prov = m_registry.get<CProvince>(e);
        if (prov.ocean) continue;

        const CStock& stock = m_registry.get<CStock>(e);
        const CPopulation& pop = m_registry.get<CPopulation>(e);

        // --- Commerce : la nourriture s'ecoule vers les voisins moins pourvus ---
        for (int q : m_neighbors[pid]) {
            entt::entity eq = m_byProvince[q];
            if (eq == entt::null) continue;
            const CProvince& provQ = m_registry.get<CProvince>(eq);
            if (provQ.ocean) continue;
            if (atWar(prov.civ, provQ.civ)) continue; // embargo entre ennemis
            const CStock& stockQ = m_registry.get<CStock>(eq);
            // On n'exporte que depuis le plus riche (chaque paire traitee une fois).
            if (stock.food > stockQ.food) {
                double share = (stock.food - stockQ.food) * tradeFrac * 0.5;
                foodDelta[pid] -= share;
                foodDelta[q] += share;
            }
        }

        // --- Migration : les affames fuient vers les voisins en surplus ---
        if (pop.lastFoodBalance < 0.0 && pop.count > 1.0) {
            double sumPos = 0.0;
            for (int q : m_neighbors[pid]) {
                entt::entity eq = m_byProvince[q];
                if (eq == entt::null) continue;
                const CProvince& provQ = m_registry.get<CProvince>(eq);
                if (provQ.ocean) continue;
                if (atWar(prov.civ, provQ.civ)) continue; // pas de refuge chez l'ennemi
                double balQ = m_registry.get<CPopulation>(eq).lastFoodBalance;
                if (balQ > 0.0) sumPos += balQ;
            }
            if (sumPos > 0.0) {
                double emigrants = pop.count * migFrac;
                for (int q : m_neighbors[pid]) {
                    entt::entity eq = m_byProvince[q];
                    if (eq == entt::null) continue;
                    const CProvince& provQ = m_registry.get<CProvince>(eq);
                    if (provQ.ocean) continue;
                    if (atWar(prov.civ, provQ.civ)) continue;
                    double balQ = m_registry.get<CPopulation>(eq).lastFoodBalance;
                    if (balQ > 0.0) {
                        double moved = emigrants * (balQ / sumPos);
                        popDelta[pid] -= moved;
                        popDelta[q] += moved;
                    }
                }
            }
        }
    }

    // Application des deltas.
    for (size_t pid = 0; pid < n; ++pid) {
        entt::entity e = m_byProvince[pid];
        if (e == entt::null) continue;
        if (m_registry.get<CProvince>(e).ocean) continue;
        CStock& stock = m_registry.get<CStock>(e);
        CPopulation& pop = m_registry.get<CPopulation>(e);
        stock.food = std::max(0.0, stock.food + foodDelta[pid]);
        pop.count = std::max(0.0, pop.count + popDelta[pid]);
    }
}

void SimulationWorld::spawnEvents(double days, int year) {
    if (m_inhabited.empty()) return;

    // Nombre d'evenements attendus sur ce pas de temps (loi ~Poisson).
    double expected = days * kEventGlobalRatePerDay;
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    int count = static_cast<int>(expected);
    if (u01(m_rng) < (expected - count)) ++count;

    std::uniform_int_distribution<size_t> pickProv(0, m_inhabited.size() - 1);
    std::uniform_int_distribution<int> pickType(0, static_cast<int>(EventType::Count) - 1);

    for (int k = 0; k < count; ++k) {
        entt::entity e = m_inhabited[pickProv(m_rng)];
        if (m_registry.any_of<CAffliction>(e)) continue; // deja afflige
        const CProvince& prov = m_registry.get<CProvince>(e);

        EventType type = static_cast<EventType>(pickType(m_rng));
        CAffliction aff;
        aff.type = type;
        int severity = 1;
        switch (type) {
            case EventType::Drought:
                aff.foodFactor = 0.35;
                aff.daysLeft = 365.0 * (1.0 + u01(m_rng) * 2.0); // 1 a 3 ans
                severity = 2;
                break;
            case EventType::Epidemic:
                aff.mortalityPerDay = 0.0015;
                aff.daysLeft = 365.0 * (0.5 + u01(m_rng)); // 0.5 a 1.5 an
                severity = 2;
                break;
            case EventType::BumperHarvest:
                aff.foodFactor = 1.6;
                aff.daysLeft = 365.0; // 1 an
                severity = 0;
                break;
            default: break;
        }
        m_registry.emplace<CAffliction>(e, aff);
        logEvent(year, std::string(eventName(type)) + " - province #"
                 + std::to_string(prov.id) + " (civ " + std::to_string(prov.civ) + ")",
                 severity);
    }
}

void SimulationWorld::logEvent(int year, std::string text, int severity) {
    m_events.push_back(EventRecord{year, std::move(text), severity});
    if (m_events.size() > 60) m_events.pop_front();
}

void SimulationWorld::recomputeAggregates() {
    double total = 0.0;
    double maxPop = 1.0;
    int inhabited = 0;
    int healthy = 0;
    std::fill(m_civPopulation.begin(), m_civPopulation.end(), 0.0);
    std::fill(m_civProvinceCount.begin(), m_civProvinceCount.end(), 0);

    auto view = m_registry.view<CProvince, CPopulation>();
    for (auto e : view) {
        const CProvince& prov = view.get<CProvince>(e);
        if (prov.ocean) continue;
        const CPopulation& pop = view.get<CPopulation>(e);
        total += pop.count;
        maxPop = std::max(maxPop, pop.count);
        ++inhabited;
        if (pop.lastFoodBalance >= 0.0) ++healthy;
        if (prov.civ >= 0 && prov.civ < m_civCount) {
            m_civPopulation[prov.civ] += pop.count;
            ++m_civProvinceCount[prov.civ];
        }
    }

    m_totalPopulation = total;
    m_maxProvincePopulation = maxPop;
    m_stability = inhabited > 0 ? static_cast<double>(healthy) / inhabited : 1.0;
}

SimulationWorld::ProvinceState SimulationWorld::state(int provinceId) const {
    ProvinceState s;
    if (provinceId < 0 || provinceId >= static_cast<int>(m_byProvince.size())) return s;
    entt::entity e = m_byProvince[provinceId];
    if (e == entt::null || !m_registry.valid(e)) return s;

    const CProvince& prov = m_registry.get<CProvince>(e);
    const CPopulation& pop = m_registry.get<CPopulation>(e);
    const CStock& stock = m_registry.get<CStock>(e);

    s.valid = true;
    s.ocean = prov.ocean;
    s.civ = prov.civ;
    s.biome = prov.biome;
    s.population = pop.count;
    s.food = stock.food;
    s.materials = stock.materials;
    s.energy = stock.energy;
    s.foodBalance = pop.lastFoodBalance;
    s.control = prov.control;
    if (const CAffliction* aff = m_registry.try_get<CAffliction>(e)) {
        s.afflicted = true;
        s.affliction = aff->type;
    }
    return s;
}

bool SimulationWorld::saveToFile(const std::string& path, int year) const {
    nlohmann::json j;
    j["year"] = year;
    auto& jp = j["provinces"];
    jp = nlohmann::json::array();
    for (size_t i = 0; i < m_byProvince.size(); ++i) {
        entt::entity e = m_byProvince[i];
        if (e == entt::null) continue;
        const CProvince& prov = m_registry.get<CProvince>(e);
        if (prov.ocean) continue;
        const CPopulation& pop = m_registry.get<CPopulation>(e);
        const CStock& st = m_registry.get<CStock>(e);
        nlohmann::json p;
        p["id"] = prov.id;
        p["civ"] = prov.civ;
        p["ctrl"] = prov.control;
        p["pop"] = pop.count;
        p["bal"] = pop.lastFoodBalance;
        p["food"] = st.food;
        p["mat"] = st.materials;
        p["en"] = st.energy;
        if (const CAffliction* a = m_registry.try_get<CAffliction>(e)) {
            p["aff"] = {{"t", static_cast<int>(a->type)}, {"d", a->daysLeft},
                        {"ff", a->foodFactor}, {"m", a->mortalityPerDay}};
        }
        jp.push_back(std::move(p));
    }
    auto& je = j["events"];
    je = nlohmann::json::array();
    for (const auto& ev : m_events) {
        je.push_back({{"y", ev.year}, {"t", ev.text}, {"s", ev.severity}});
    }

    std::ofstream f(path);
    if (!f) return false;
    f << j.dump(1, '\t');
    return true;
}

bool SimulationWorld::loadFromFile(const std::string& path, int& yearOut) {
    std::ifstream f(path);
    if (!f) return false;
    nlohmann::json j;
    try { f >> j; } catch (...) { return false; }

    yearOut = j.value("year", -3000);
    m_registry.clear<CAffliction>(); // on repart d'afflictions vierges

    if (j.contains("provinces")) {
        for (const auto& p : j["provinces"]) {
            int id = p.value("id", -1);
            if (id < 0 || id >= static_cast<int>(m_byProvince.size())) continue;
            entt::entity e = m_byProvince[id];
            if (e == entt::null) continue;
            CProvince& pr = m_registry.get<CProvince>(e);
            pr.civ = p.value("civ", -1);
            pr.control = p.value("ctrl", 0.0);
            pr.contender = -1;
            CPopulation& pop = m_registry.get<CPopulation>(e);
            CStock& st = m_registry.get<CStock>(e);
            pop.count = p.value("pop", 0.0);
            pop.lastFoodBalance = p.value("bal", 0.0);
            st.food = p.value("food", 0.0);
            st.materials = p.value("mat", 0.0);
            st.energy = p.value("en", 0.0);
            if (p.contains("aff")) {
                const auto& ja = p["aff"];
                CAffliction a;
                a.type = static_cast<EventType>(ja.value("t", 0));
                a.daysLeft = ja.value("d", 0.0);
                a.foodFactor = ja.value("ff", 1.0);
                a.mortalityPerDay = ja.value("m", 0.0);
                m_registry.emplace_or_replace<CAffliction>(e, a);
            }
        }
    }

    m_events.clear();
    if (j.contains("events")) {
        for (const auto& ev : j["events"]) {
            m_events.push_back(EventRecord{ev.value("y", 0),
                                           ev.value("t", std::string()),
                                           ev.value("s", 1)});
        }
    }

    recomputeAggregates();
    return true;
}

std::vector<SimulationWorld::ProvinceState> SimulationWorld::allStates() const {
    std::vector<ProvinceState> out(m_byProvince.size());
    for (size_t i = 0; i < m_byProvince.size(); ++i) {
        out[i] = state(static_cast<int>(i));
    }
    return out;
}

double SimulationWorld::population(int provinceId) const {
    if (provinceId < 0 || provinceId >= static_cast<int>(m_byProvince.size())) return 0.0;
    entt::entity e = m_byProvince[provinceId];
    if (e == entt::null || !m_registry.valid(e)) return 0.0;
    return m_registry.get<CPopulation>(e).count;
}

} // namespace wl
