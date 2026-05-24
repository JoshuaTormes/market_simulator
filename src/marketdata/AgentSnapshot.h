#pragma once
#include "MarketSnapshot.h"
#include "InformationProfile.h"
#include "core/RngService.h"
#include <random>
#include <cmath>

// Agent-specific view of the market: derived from MarketSnapshot + InformationProfile.
// The perceived prices add idiosyncratic Gaussian noise; delayed snapshots (info_delay_ticks > 0)
// require the caller to supply the appropriate historical snapshot.
struct AgentSnapshot {
    MarketSnapshot base;        // original (possibly historical) snapshot
    Price perceived_mid = 0;    // mid_price + N(0, sigma)
    Price perceived_last = 0;   // last_trade_price + N(0, sigma)
    double fundamental_value = 0.0; // filled only if sees_fundamental
    bool   has_fundamental   = false;
    double own_inventory = 0.0; // signed lot position from PositionLedger (filled by AgentRunner)

    // Derive from a (possibly historical) MarketSnapshot.
    // rng_eng must be pre-seeded from RngService::for_consumer(agent_id + tick).
    static AgentSnapshot derive_from(
        const MarketSnapshot& snap,
        const InformationProfile& info,
        std::mt19937_64& rng_eng
    ) {
        AgentSnapshot as;
        as.base = snap;

        if (info.sigma_price_noise > 0.0) {
            std::normal_distribution<double> noise(0.0, info.sigma_price_noise);
            as.perceived_mid  = snap.mid_price  + static_cast<Price>(std::round(noise(rng_eng)));
            as.perceived_last = snap.last_trade_price + static_cast<Price>(std::round(noise(rng_eng)));
        } else {
            as.perceived_mid  = snap.mid_price;
            as.perceived_last = snap.last_trade_price;
        }

        return as;
    }
};
