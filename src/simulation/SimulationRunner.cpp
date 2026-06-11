#include "simulation/SimulationRunner.h"

#include <chrono>

namespace wl {

using clock_t = std::chrono::steady_clock;

SimulationRunner::~SimulationRunner() {
    stop();
}

SimulationRunner::Snapshot SimulationRunner::buildSnapshot(int year) const {
    Snapshot snap;
    snap.year = year;
    snap.totalPopulation = m_world.totalPopulation();
    snap.stability = m_world.stability();
    snap.maxProvincePopulation = m_world.maxProvincePopulation();
    snap.provinces = m_world.allStates();
    const auto& ev = m_world.events();
    snap.events.assign(ev.begin(), ev.end());
    snap.civCount = m_world.civCount();
    snap.opinion = m_world.opinionMatrix();
    snap.civPopulation = m_world.civPopulations();
    snap.civTech = m_world.civTech();
    return snap;
}

void SimulationRunner::start(const ProvinceMap& provinces, float seaLevel) {
    stop();

    // Initialisation sur le thread appelant (ne touche pas OpenGL).
    m_world.init(provinces, seaLevel);

    // Premier snapshot pour que la premiere frame ait des donnees.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_published = buildSnapshot(-3000);
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

        // Traitement d'une eventuelle requete de sauvegarde / chargement.
        IoOp op = IoOp::None;
        std::string path;
        {
            std::lock_guard<std::mutex> lock(m_ioMutex);
            op = m_ioOp;
            path = m_ioPath;
            m_ioOp = IoOp::None;
        }
        if (op == IoOp::Save) {
            m_world.saveToFile(path, m_year);
        } else if (op == IoOp::Load) {
            int y = m_year;
            if (m_world.loadFromFile(path, y)) {
                m_year = y;
                m_dayOfYear = 0.0;
            }
        }

        if (!m_paused.load()) {
            double days = dt * m_speed.load() * 10.0; // 10 jours / s a x1
            m_dayOfYear += days;
            while (m_dayOfYear >= 365.0) {
                m_dayOfYear -= 365.0;
                ++m_year;
            }
            m_world.tick(days, m_year);
        }

        // Publication de l'instantane (copie courte sous mutex).
        Snapshot snap = buildSnapshot(m_year);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_published = std::move(snap);
        }

        // Cadence de simulation ~100 Hz (le dt mesure garde le temps reel exact).
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

SimulationRunner::Snapshot SimulationRunner::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_published;
}

void SimulationRunner::requestSave(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    m_ioOp = IoOp::Save;
    m_ioPath = path;
}

void SimulationRunner::requestLoad(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    m_ioOp = IoOp::Load;
    m_ioPath = path;
}

} // namespace wl
