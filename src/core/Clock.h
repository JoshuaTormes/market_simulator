#pragma once
#include "Types.h"
#include <chrono>
#include <functional>

// Multi-mode simulation clock.
// RealTime:         sleeps between ticks to honour wall-clock secondsPerTick.
// Accelerated:      sleeps for secondsPerTick / factor.
// AsFastAsPossible: no sleep — maximum throughput.

enum class ClockMode {
    RealTime,
    Accelerated,
    AsFastAsPossible
};

class Clock {
public:
    Clock(ClockMode mode, double seconds_per_tick, double accel_factor = 1.0);

    // Run onTick(t) for t in [0, max_ticks).
    void run(Tick max_ticks, std::function<void(Tick)> on_tick);

    Tick current_tick() const { return current_tick_; }
    double seconds_per_tick() const { return seconds_per_tick_; }
    ClockMode mode() const { return mode_; }

    // Runtime adjustment — call from UI thread; sim thread reads atomically.
    void set_speed_multiplier(double factor);

private:
    ClockMode mode_;
    double seconds_per_tick_;
    double accel_factor_;
    Tick current_tick_{0};
};
