#pragma once

#include "simulation/SimulationWorld.h"

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

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
        double timeDays = 0.0; // temps absolu in-game (meteo / saisons)
        double totalPopulation = 0.0;
        double stability = 1.0;
        double maxProvincePopulation = 1.0;
        std::vector<SimulationWorld::ProvinceState> provinces;
        std::vector<SimulationWorld::EventRecord> events;
        int civCount = 0;
        std::vector<float> opinion;          // matrice civ x civ (a plat)
        std::vector<double> civPopulation;   // population par civ
        std::vector<double> civTech;         // points de technologie par civ
        std::vector<int> civProvinceCount;   // nombre de provinces par civ
        int cloudW = 0;                      // champ nuageux simule (cycle de l'eau)
        int cloudH = 0;
        std::vector<float> cloud;            // couverture nuageuse par cellule (0..1)
    };

    ~SimulationRunner();

    // Initialise un monde par planete (thread de rendu) puis lance le thread de
    // simulation qui les fait tous evoluer en parallele.
    void start(const std::vector<const ProvinceMap*>& planets, float seaLevel = 0.0f);
    void stop();

    int planetCount() const { return static_cast<int>(m_worlds.size()); }

    // Controles (thread-safe via atomiques).
    void setPaused(bool p) { m_paused.store(p); }
    void setSpeed(int s) { m_speed.store(s); }
    bool paused() const { return m_paused.load(); }
    int speed() const { return m_speed.load(); }

    // Cote rendu : copie du dernier instantane publie de la planete `planet`.
    Snapshot snapshot(int planet) const;

    // Sauvegarde / chargement de la planete `planet`.
    void requestSave(int planet, const std::string& path);
    void requestLoad(int planet, const std::string& path);

private:
    void run();
    Snapshot buildSnapshot(SimulationWorld& world, int year) const;

    enum class IoOp { None, Save, Load };

    int m_year = -3000;
    double m_dayOfYear = 0.0;
    double m_publishAccum = 0.0;
    double m_interAccum = 0.0; // accumulation pour les colonies interplanetaires

    std::vector<std::unique_ptr<SimulationWorld>> m_worlds;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<int> m_speed{1};

    mutable std::mutex m_mutex;
    std::vector<Snapshot> m_published;

    std::mutex m_ioMutex;
    IoOp m_ioOp = IoOp::None;
    int m_ioPlanet = 0;
    std::string m_ioPath;
};

} // namespace wl
