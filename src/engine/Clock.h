#pragma once
#include <cstdint>
#include <functional>

class Clock {
public:
    Clock(uint64_t maxTicks, double secondsPerTick);

    template<typename Callback>
    void run(Callback onTick);

    uint64_t getCurrentTick() const;
    double getSecondsPerTick() const;

private:
    uint64_t maxTicks;
    double secondsPerTick;
    uint64_t currentTick;
};

template<typename Callback>
void Clock::run(Callback onTick) {
    for (currentTick = 0; currentTick < maxTicks; ++currentTick) {
        onTick(currentTick);
    }
}
