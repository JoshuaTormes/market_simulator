#include "Clock.h"
#include <thread>
#include <atomic>

Clock::Clock(ClockMode mode, double seconds_per_tick, double accel_factor)
    : mode_(mode),
      seconds_per_tick_(seconds_per_tick),
      accel_factor_(accel_factor > 0.0 ? accel_factor : 1.0)
{}

void Clock::set_speed_multiplier(double factor) {
    accel_factor_ = factor > 0.0 ? factor : 1.0;
}

void Clock::run(Tick max_ticks, std::function<void(Tick)> on_tick) {
    using namespace std::chrono;
    using steady = steady_clock;

    for (current_tick_ = 0; current_tick_ < max_ticks; ++current_tick_) {
        auto t_start = steady::now();

        on_tick(current_tick_);

        if (mode_ == ClockMode::AsFastAsPossible)
            continue;

        double effective_spt = seconds_per_tick_;
        if (mode_ == ClockMode::Accelerated)
            effective_spt /= accel_factor_;

        auto elapsed = steady::now() - t_start;
        auto target  = duration_cast<nanoseconds>(duration<double>(effective_spt));
        if (target > elapsed)
            std::this_thread::sleep_for(target - elapsed);
    }
}
