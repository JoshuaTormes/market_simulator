#pragma once
// Informed trader with linear price impact: Kyle (1985).
//
// The trader sees the latent fundamental through multiplicative noise,
// S = V·exp(ε), and trades the *gap* between that private value and the
// quoted mid.  Two properties make it a price-discovery mechanism rather than
// a one-way accumulator:
//
//   * it only crosses the spread when the gap is wider than the cost of
//     crossing it, so the mid is pushed toward V and then left alone;
//   * once the gap fits inside the spread the position is unwound, which
//     releases the inventory the earlier trades built up.
//
// The previous version compared V to the mid with an additive noise measured
// in ticks (0.005 of a tick — effectively no noise), ignored the spread, and
// never unwound, so both informed traders sat pinned at their position limit
// for 94% of the run and stopped trading altogether.
#include "AgentBase.h"

class InformedTraderKyle : public AgentBase {
public:
    struct Params {
        double signal_noise_log = 5e-4; // sigma of the log-noise on the observed V
        double lambda_inv       = 0.5;  // lots per tick of gap (inverse Kyle lambda)
        double margin_ticks     = 1.0;  // gap must beat half-spread by this much
        Qty    max_order_size   = 200;  // cap per aggressive order
        Qty    unwind_qty       = 50;   // lots returned per tick when the gap is closed
        double pareto_alpha     = 1.5;
        // Own risk appetite as a fraction of the hard position limit.  The
        // limit belongs to the broker; the trader's own budget is smaller, and
        // sizing against it is what keeps the trader off the gate — an agent
        // pinned at the gate has stopped discovering anything, because the
        // rejected side of its flow carries no information into the book.
        double q_soft_frac      = 0.6;  // heavy-tailed size multiplier (0 = pure Kyle)
    };

    InformedTraderKyle(AgentId id, const std::string& ticker,
                       std::mt19937_64 rng, Params p,
                       RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "InformedTraderKyle"; }

private:
    Params p_;
    std::normal_distribution<double> log_noise_;
};
