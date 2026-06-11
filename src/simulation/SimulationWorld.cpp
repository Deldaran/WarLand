#include "simulation/SimulationWorld.h"
#include "renderer/overlay/ProvinceMap.h"

#include <algorithm>
#include <cmath>

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

void SimulationWorld::init(const ProvinceMap& provinces, float seaLevel) {
    m_registry.clear();
    const int n = provinces.provinceCount();
    m_byProvince.assign(n, entt::null);

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
    }

    recomputeAggregates();
}

void SimulationWorld::tick(double days) {
    if (days <= 0.0) return;
    days = std::min(days, 10.0); // garde-fou contre les gros pas de temps

    auto view = m_registry.view<CProvince, CPopulation, CStock>();
    for (auto e : view) {
        const CProvince& prov = view.get<CProvince>(e);
        if (prov.ocean) continue;

        CPopulation& pop = view.get<CPopulation>(e);
        CStock& stock = view.get<CStock>(e);
        const BiomeYield& y = kYields[static_cast<int>(prov.biome)];

        double workers = std::max(0.0, pop.count);
        double sq = std::sqrt(workers);

        // --- Nourriture : moteur de la dynamique de population ---
        double foodProd = y.food * sq * kFoodProdFactor;
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

    recomputeAggregates();
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
    return s;
}

double SimulationWorld::population(int provinceId) const {
    if (provinceId < 0 || provinceId >= static_cast<int>(m_byProvince.size())) return 0.0;
    entt::entity e = m_byProvince[provinceId];
    if (e == entt::null || !m_registry.valid(e)) return 0.0;
    return m_registry.get<CPopulation>(e).count;
}

} // namespace wl
