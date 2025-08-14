#include "Scheduler.h"
#include <vector>
#include <functional>
#include <algorithm>

struct SystemEntry { int order; std::function<void(double)> tick; bool enabled; };

struct SchedulerImpl {
public:
    std::vector<SystemEntry> systems;
};

Scheduler::Scheduler() : impl_(new SchedulerImpl) {}
Scheduler::~Scheduler() { delete impl_; }

void Scheduler::addSystem(int order, std::function<void(double)> onFixedUpdate, bool enabled) {
    impl_->systems.push_back({order, std::move(onFixedUpdate), enabled});
}

void Scheduler::setEnabled(size_t idx, bool e) { if (idx < impl_->systems.size()) impl_->systems[idx].enabled = e; }

void Scheduler::updateFixed(double dt) {
    std::stable_sort(impl_->systems.begin(), impl_->systems.end(), [](const auto& a, const auto& b){ return a.order < b.order; });
    for (auto& s : impl_->systems) if (s.enabled && s.tick) s.tick(dt);
}
