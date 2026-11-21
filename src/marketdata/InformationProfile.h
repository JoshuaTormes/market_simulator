#pragma once
#include "core/Types.h"

// Controls what an agent perceives vs reality.
struct InformationProfile {
    double sigma_price_noise = 0.0;  // stdev of additive Gaussian noise on perceived prices (in ticks)
    Tick   info_delay_ticks  = 0;    // agent sees market from `now - delay` (0 = real-time)
    bool   sees_fundamental  = false; // can observe the FundamentalValueProcess
};
