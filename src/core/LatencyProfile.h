#pragma once
#include "Types.h"

// Per-agent latency model: orders arrive after base + lognormal jitter ticks.
struct LatencyProfile {
    Tick   base_latency_ticks      = 0;
    double jitter_lognormal_sigma  = 0.0;  // 0 = no jitter
};
