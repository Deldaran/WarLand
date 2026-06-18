#include "simulation/SimulationRunner.h"

#include <chrono>

namespace wl {

using clock_t = std::chrono::steady_clock;

SimulationRunner::~SimulationRunner() {
    stop();
}

SimulationRunner::Snapshot SimulationRunner::buildSnapshot(SimulationWorld& world, int year) const {
    Snapshot snap;
    snap.year = year;
    snap.timeDays = year * 365.0 + m_dayOfYear;
    snap.totalPopulation = world.totalPopulation();
    snap.stability = world.stability();
    snap.maxProvincePopulation = world.maxProvincePopulation();
    snap.provinces = world.allStates();
    const auto& ev = world.events();
    snap.events.assign(ev.begin(), ev.end());
    snap.civCount = world.civCount();
    snap.opinion = world.opinionMatrix();
    snap.civPopulation = world.civPopulations();
    snap.civTech = world.civTech();
    snap.civProvinceCount = world.civProvinceCounts();
    snap.cloudW = world.climateWidth();
    snap.cloudH = world.climateHeight();
    snap.cloud = world.cloudField();
    return snap;
}

void SimulationRunner::start(const std::vector<const ProvinceMap*>& planets, float seaLevel) {
    stop();

    // Un monde par planete, initialise sur le thread appelant (pas d'OpenGL).
    m_worlds.clear();
    m_published.clear();
    for (const ProvinceMap* pm : planets) {
        auto world = std::make_unique<SimulationWorld>();
        world->init(*pm, seaLevel);
        m_published.push_back(buildSnapshot(*world, -3000));
        m_worlds.push_back(std::move(world));
    }

    m_running.store(true);
    m_thread = std::thread(&SimulationRunner::run, this);
}

void SimulationRunner::stop() {
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
}

void SimulationRunner::run() {
    m_year = -3000;
    m_dayOfYear = 0.0;
    auto last = clock_t::now();

    while (m_running.load()) {
        auto now = clock_t::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        // Requete eventuelle de sauvegarde / chargement (sur une planete donnee).
        IoOp op = IoOp::None;
        std::string path;
        int planet = 0;
        {
            std::lock_guard<std::mutex> lock(m_ioMutex);
            op = m_ioOp;
            path = m_ioPath;
            planet = m_ioPlanet;
            m_ioOp = IoOp::None;
        }
        if (planet >= 0 && planet < static_cast<int>(m_worlds.size())) {
            if (op == IoOp::Save) {
                m_worlds[planet]->saveToFile(path, m_year);
            } else if (op == IoOp::Load) {
                int y = m_year;
                if (m_worlds[planet]->loadFromFile(path, y)) m_year = y;
            }
        }

        if (!m_paused.load()) {
            double days = dt * m_daysPerSec.load(); // echelle de temps choisie
            m_dayOfYear += days;
            while (m_dayOfYear >= 365.0) {
                m_dayOfYear -= 365.0;
                ++m_year;
            }
            double timeDays = m_year * 365.0 + m_dayOfYear;
            for (auto& w : m_worlds) w->tick(days, m_year, timeDays); // toutes les planetes

            // Colonisation interplanetaire : une civ avancee essaime vers un autre
            // monde (tentative tous les ~40 ans in-game).
            m_interAccum += days;
            if (m_interAccum >= 40.0 * 365.0 && m_worlds.size() > 1) {
                m_interAccum = 0.0;
                for (size_t s = 0; s < m_worlds.size(); ++s) {
                    int culture = -1;
                    if (m_worlds[s]->pickSpacefaringCulture(culture)) {
                        size_t t = (s + 1) % m_worlds.size(); // planete cible
                        m_worlds[t]->foundColony(culture, m_year);
                        break; // une colonie par vague
                    }
                }
            }
        }

        // Publication throttlee a ~30 Hz (tous les mondes).
        m_publishAccum += dt;
        if (m_publishAccum >= 0.033) {
            m_publishAccum = 0.0;
            std::vector<Snapshot> snaps;
            snaps.reserve(m_worlds.size());
            for (auto& w : m_worlds) snaps.push_back(buildSnapshot(*w, m_year));
            std::lock_guard<std::mutex> lock(m_mutex);
            m_published = std::move(snaps);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

SimulationRunner::Snapshot SimulationRunner::snapshot(int planet) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (planet < 0 || planet >= static_cast<int>(m_published.size())) return Snapshot{};
    return m_published[planet];
}

void SimulationRunner::requestSave(int planet, const std::string& path) {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    m_ioOp = IoOp::Save;
    m_ioPlanet = planet;
    m_ioPath = path;
}

void SimulationRunner::requestLoad(int planet, const std::string& path) {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    m_ioOp = IoOp::Load;
    m_ioPlanet = planet;
    m_ioPath = path;
}

} // namespace wl
