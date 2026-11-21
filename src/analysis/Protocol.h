#pragma once
// Calibration / validation protocol, frozen before any mechanism change.
//
// CAL metrics may guide parameter tuning and are measured on the CAL seeds.
// VAL metrics are the hold-out: they judge a change and never guide one.  A
// parameter chosen by looking at a VAL number contaminates the validation;
// when it happens it is written down in the plan and the VAL seeds are
// replaced by a fresh range.
//
// A change counts as an improvement only when the paired difference across
// seeds exceeds kSignificanceSE standard errors of that difference.
//
// Everything here is a constant.  sim_calibrate checks at start-up that every
// key listed below names a metric it actually computes.
#include <cstdint>

namespace protocol {

enum class Set { Cal, Val, Diag };

inline const char* to_string(Set s) {
    switch (s) {
        case Set::Cal:  return "CAL";
        case Set::Val:  return "VAL";
        case Set::Diag: return "diag";
    }
    return "?";
}

struct SeedRange { uint64_t first; int count; };
inline constexpr SeedRange kCalSeeds{1, 10};     // seeds 1..10
inline constexpr SeedRange kValSeeds{101, 20};   // seeds 101..120

// Calibration set: the quantities the model is allowed to be tuned to.
inline constexpr const char* kCalMetrics[] = {
    "cal.vol_ann_1m",      // realized annual vol of the mid, 1-min returns
    "cal.spread_bp",       // mean quoted spread, basis points
    "cal.u_volume",        // intraday U of volume (open+close over mid-session)
    "cal.mm_top_share",    // largest single maker's share of maker volume
    "cal.gap_half_life",   // AR(1) half-life of log(mid/V), ticks
};

// Validation set: the hold-out.  The v2 battery, the metaorder impact
// exponent and price discovery of the diffusion part of V alone.
inline constexpr const char* kValMetrics[] = {
    "val.v2.1",  "val.v2.2",  "val.v2.3",  "val.v2.4",  "val.v2.5",
    "val.v2.6",  "val.v2.7",  "val.v2.8",  "val.v2.9",  "val.v2.10",
    "val.v2_pass",
    "val.delta_meta",      // metaorder impact exponent
    "val.pd_diff_h20",     // corr(r_mid, r_diffusion), h = 20
    "val.pd_diff_h100",    // same, h = 100
};

// Targets that the protocol fixes (the rest live with each metric).
inline constexpr double kVolAnnualTarget = 0.30;   // = FundamentalConfig::sigma_annual
// Metaorder impact exponent measured on real markets: Tóth et al. (2011),
// Bouchaud et al. (2018, ch. 12) report δ between ~0.4 and ~0.7.
inline constexpr double kDeltaLo = 0.4, kDeltaHi = 0.7;

inline constexpr double kSignificanceSE = 2.0;

}  // namespace protocol
