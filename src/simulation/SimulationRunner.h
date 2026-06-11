#pragma once

#include "simulation/SimulationWorld.h"

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

namespace wl {

class ProvinceMap;

// Execute la simulation sur son propre thread, decouplee du rendu.
// Le thread de simulation avance le temps en temps reel (selon vitesse/pause)
// et publie un instantane (Snapshot) protege par mutex. Le thread de rendu
// lit ce snapshot a chaque frame -> 60 FPS garantis meme si la simulation
// devient lourde, et acceleration du temps sans bloquer l'affichage.
class SimulationRunner {
public:
    struct Snapshot {
        int year = -3000;
        double totalPopulation = 0.0;
        double stability = 1.0;
        double maxProvincePopulation = 1.0;
        std::vector<SimulationWorld::ProvinceState> provinces;
        std::vector<SimulationWorld::EventRecord> events;
        int civCount = 0;
        std::vector<float> opinion;          // matrice civ x civ (a plat)
        std::vector<double> civPopulation;   // population par civ
        std::vector<double> civTech;         // points de technologie par civ
    };

    ~SimulationRunner();

    // Initialise le monde (thread de rendu) puis lance le thread de simulation.
    void start(const ProvinceMap& provinces, float seaLevel = 0.0f);
    void stop();

    // Controles (thread-safe via atomiques).
    void setPaused(bool p) { m_paused.store(p); }
    void setSpeed(int s) { m_speed.store(s); }
    bool paused() const { return m_paused.load(); }
    int speed() const { return m_speed.load(); }

    // Cote rendu : copie du dernier instantane publie.
    Snapshot snapshot() const;

    // Sauvegarde / chargement : la requete est executee par le thread de
    // simulation (aucun acces concurrent au monde).
    void requestSave(const std::string& path);
    void requestLoad(const std::string& path);

private:
    void run();
    Snapshot buildSnapshot(int year) const;

    enum class IoOp { None, Save, Load };

    int m_year = -3000;
    double m_dayOfYear = 0.0;

    SimulationWorld m_world;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<int> m_speed{1};

    mutable std::mutex m_mutex;
    Snapshot m_published;

    std::mutex m_ioMutex;
    IoOp m_ioOp = IoOp::None;
    std::string m_ioPath;
};

} // namespace wl
