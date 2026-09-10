#pragma once
// TWAP execution of large parent orders sliced into child orders, with a
// Poisson arrival process for the parents themselves.
//
// The previous version was handed one parent order at construction and went
// permanently silent once it was done: past the first few hundred ticks the
// desk contributed nothing, so the heavy-tailed flow it exists to produce was
// confined to the start of every run.  A real desk receives orders throughout
// the session, in both directions, of widely varying size — that is what makes
// order splitting a source of fat tails and of positive trade-sign
// autocorrelation (Lillo, Mike & Farmer 2005).
//
// So: each tick, with probability arrival_lambda, a new parent arrives (side
// drawn fairly, size Pareto-distributed and truncated) and is worked in
// `total_slices` children spaced `ticks_between` ticks apart.  Only one parent
// is worked at a time; an arrival while busy is ignored, which caps the desk's
// participation rate without needing a queue.
#include "AgentBase.h"
#include <random>

class InstitutionalExecutor : public AgentBase {
public:
    struct Params {
        // Parent arrival process.
        double arrival_lambda = 0.002;  // P(new parent | tick), when idle
        Qty    parent_min     = 200;    // Pareto support is truncated to
        Qty    parent_max     = 3000;   // [parent_min, parent_max]
        double parent_alpha   = 1.5;    // Pareto tail exponent for parent size
        // Inventory scale of the parent-side tilt, in lots.  A desk works
        // client orders and passes the position through; with no client in the
        // simulator a fair coin lets the desk's own book random-walk into the
        // risk limit, where the gate rejects one side and the desk becomes a
        // permanent directional push.  Tilting which parents it accepts against
        // its current book is the stand-in for that flow being two-sided.
        double q_scale        = 1500.0;
        // Execution schedule.
        int    total_slices   = 20;     // children per parent
        int    ticks_between  = 5;      // ticks between children
        double pareto_alpha   = 1.5;    // Pareto tail exponent for child sizing (0 = uniform)
        Qty    max_child_qty  = 200;    // hard cap per child order
    };

    InstitutionalExecutor(AgentId id, const std::string& ticker,
                          std::mt19937_64 rng, Params p,
                          RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "InstitutionalExecutor"; }

    // Diagnostics: how many parent orders have been accepted so far.
    int parents_started() const { return parents_started_; }

private:
    // Draw a Pareto(1, alpha) variate truncated to [lo, hi] lots.
    Qty draw_parent_qty();

    Params p_;
    std::bernoulli_distribution arrival_;
    std::uniform_real_distribution<double> u_;

    // Current parent being worked; remaining_ == 0 means idle.
    Side   side_        = Side::Buy;
    Qty    remaining_   = 0;
    int    slices_done_ = 0;
    Tick   next_tick_   = 0;
    int    parents_started_ = 0;
};
