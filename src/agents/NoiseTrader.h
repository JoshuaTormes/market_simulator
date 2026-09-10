#pragma once
// Uninformed noise trader: near-random direction, lognormal size.
//
// "Uninformed" is not the same as "unconstrained".  A trader that flips a fair
// coin every tick performs a random walk in inventory, so after 50k ticks its
// position is O(sqrt(n)) lots away from flat and the whole cohort spends its
// time pinned against the risk limit, where the gate rejects one side of its
// flow and the remaining side becomes a persistent directional push.  That is
// an artefact, not liquidity demand.
//
// The fix is the weakest possible form of inventory management: the side
// probability is tilted against the current position,
//     P(buy) = 0.5 - 0.5 * tanh(q / q_scale),
// which is a fair coin at q = 0 and saturates to one-sided liquidation far from
// flat.  The trader still has no view on price; it only declines to keep
// doubling a position it never wanted.
#include "AgentBase.h"
#include <random>

class NoiseTrader : public AgentBase {
public:
    struct Params {
        double p_act      = 0.05;  // probability of acting each tick (Bernoulli)
        double size_mu    = 1.5;   // log-mean of order size (lognormal)
        double size_sigma = 1.5;   // log-std of order size
        Qty    max_size   = 200;
        // Inventory scale of the side tilt, in lots.  At |q| = q_scale the
        // coin is already ~76/24 against the position; at 3·q_scale it is
        // effectively one-sided.
        double q_scale    = 500.0;
    };

    NoiseTrader(AgentId id, const std::string& ticker,
                std::mt19937_64 rng, Params p,
                RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "NoiseTrader"; }

private:
    Params p_;
    std::bernoulli_distribution  act_;
    std::uniform_real_distribution<double> side_u_;
    std::lognormal_distribution<double> size_dist_;
};
