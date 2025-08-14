#pragma once
#include <cstddef>
#include <functional>

// [7] Minimal fixed-timestep scheduler to orchestrate systems.
class Scheduler {
  public:
    Scheduler();
    ~Scheduler();

    // Register a system callback with an execution order. Lower order runs first.
    void addSystem(int order, std::function<void(double)> onFixedUpdate,
                   bool enabled = true);
    // Enable/disable a previously registered system by index (in registration order).
    void setEnabled(size_t idx, bool enabled);

    // Execute all enabled systems in order with the given fixed delta time.
    void updateFixed(double dt);

  private:
    struct SchedulerImpl* impl_;
};
