#include "Clock.h"

Clock::Clock(uint64_t maxTicks, double secondsPerTick)
    : maxTicks(maxTicks), secondsPerTick(secondsPerTick), currentTick(0) {}

uint64_t Clock::getCurrentTick() const { return currentTick; }
double Clock::getSecondsPerTick() const { return secondsPerTick; }
