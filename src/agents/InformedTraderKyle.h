#pragma once
// Informed trader as a target-position strategy: Kyle (1985) in the form the
// empirical literature actually finds.
//
// The trader sees the latent fundamental through multiplicative noise,
// S = V·exp(ε), and holds a position proportional to the mispricing that
// survives its own costs: q* = κ · gap_eff, where gap_eff is the part of the
// gap S − mid that exceeds the half-spread, a margin and one sigma of the
// trader's own signal noise.  Each tick it moves a fraction of the way from
// its current position to q* — Kyle's insider spreads trades over time, and a
// gradual schedule is what gives the price a chance to move before the next
// slice, so the trader does not pay its own impact.
//
// The previous version traded a *flow* proportional to the gap and unwound
// 50 lots per tick whenever the gap dipped inside the spread.  On a noisy
// signal that produced a buy/sell churn around the threshold, and the churn
// paid the spread to the makers on every leg: the informed traders were the
// biggest losers in the ecology, which no microstructure model allows.
#include "AgentBase.h"

class InformedTraderKyle : public AgentBase {
public:
    struct Params {
        double signal_noise_log      = 5e-4; // sigma of the log-noise on the observed V
        double target_lots_per_tick  = 40.0; // κ: target position per tick of effective gap
        double speed                 = 0.10; // fraction of (q* − q) traded per tick
        double deadband_sigma        = 1.0;  // extra threshold in sigmas of the own signal noise
        double margin_ticks          = 1.0;  // gap must beat the half-spread by this much
        Qty    max_order_size        = 200;  // cap per aggressive order
        Qty    min_order             = 5;    // below this the slice is not worth the spread
        // > 0: margin_ticks and target_lots_per_tick are read at this price and
        // scaled with mid, so the trader reacts to a relative mispricing.
        double price_ref_ticks       = 0.0;
        double pareto_alpha          = 1.5;  // heavy-tailed size multiplier (0 = pure Kyle)
        // Own risk appetite as a fraction of the hard position limit.  The
        // limit belongs to the broker; the trader's own budget is smaller, and
        // a trader pinned at the gate has stopped discovering anything.
        double q_soft_frac           = 0.6;
    };

    InformedTraderKyle(AgentId id, const std::string& ticker,
                       std::mt19937_64 rng, Params p,
                       RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "InformedTraderKyle"; }

    // Last target position, in lots — exposed for tests and diagnostics.
    double target() const { return target_; }

private:
    Params p_;
    double target_ = 0.0;
    std::normal_distribution<double>       log_noise_;
    std::uniform_real_distribution<double> u_{0.0, 0.9999};
};
