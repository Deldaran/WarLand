#include "simulation/SimulationWorld.h"
#include "renderer/overlay/ProvinceMap.h"

#include <algorithm>
#include <cmath>
#include <iostream>

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

void SimulationWorld::init(const ProvinceMap& provinces, float seaLevel) {
    m_registry.clear();
    m_events.clear();
    m_inhabited.clear();
    const int n = provinces.provinceCount();
    m_byProvince.assign(n, entt::null);

    // Copie du graphe d'adjacence pour le commerce / la migration.
    m_neighbors.assign(n, {});
    for (int p = 0; p < n; ++p) m_neighbors[p] = provinces.neighbors(p);

    for (int p = 0; p < n; ++p) {
        entt::entity e = m_registry.create();
        m_byProvince[p] = e;

        float elev = provinces.provinceElevation(p);
        float lat = provinces.provinceLatitude(p);
        Biome biome = deriveBiome(elev, lat, seaLevel);
        bool ocean = (biome == Biome::Ocean);

        m_registry.emplace<CProvince>(e, CProvince{p, provinces.provinceCiv(p), biome, ocean});

        const BiomeYield& y = kYields[static_cast<int>(biome)];

        double pop = 0.0;
        if (!ocean) {
            // Capacite de charge ~ (100 * yield_food * factor / conso)^2.
            double cap = std::pow(100.0 * y.food * kFoodProdFactor, 2.0);
            pop = std::max(200.0, cap * 0.25); // demarre a 25% de la capacite
        }
        m_registry.emplace<CPopulation>(e, CPopulation{pop, 0.0});
        m_registry.emplace<CStock>(e, CStock{ocean ? 0.0 : 50.0,
                                             ocean ? 0.0 : 20.0,
                                             ocean ? 0.0 : 20.0});
        if (!ocean) m_inhabited.push_back(e);
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
    spawnEvents(days, year);
    recomputeAggregates();
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

    auto view = m_registry.view<CProvince, CPopulation>();
    for (auto e : view) {
        const CProvince& prov = view.get<CProvince>(e);
        if (prov.ocean) continue;
        const CPopulation& pop = view.get<CPopulation>(e);
        total += pop.count;
        maxPop = std::max(maxPop, pop.count);
        ++inhabited;
        if (pop.lastFoodBalance >= 0.0) ++healthy;
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
    if (const CAffliction* aff = m_registry.try_get<CAffliction>(e)) {
        s.afflicted = true;
        s.affliction = aff->type;
    }
    return s;
}

double SimulationWorld::population(int provinceId) const {
    if (provinceId < 0 || provinceId >= static_cast<int>(m_byProvince.size())) return 0.0;
    entt::entity e = m_byProvince[provinceId];
    if (e == entt::null || !m_registry.valid(e)) return 0.0;
    return m_registry.get<CPopulation>(e).count;
}

} // namespace wl
