#pragma once
// TWAP execution of a large parent order sliced into equal child orders.
// Minimises market impact: Almgren & Chriss (2001).
#include "AgentBase.h"

class InstitutionalExecutor : public AgentBase {
public:
    struct Params {
        Side   side          = Side::Buy;
        Qty    parent_qty    = 500;   // total lots to execute
        int    total_slices  = 20;    // number of child orders
        int    ticks_between = 5;     // ticks between child orders
        double pareto_alpha  = 1.5;   // Pareto tail exponent for child sizing (0 = uniform)
        Qty    max_child_qty = 200;   // hard cap per child order
    };

    InstitutionalExecutor(AgentId id, const std::string& ticker,
                          std::mt19937_64 rng, Params p,
                          RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "InstitutionalExecutor"; }

private:
    Params p_;
    Qty    remaining_   = 0;
    int    slices_done_ = 0;
    Tick   next_tick_   = 0;
    bool   started_     = false;
};
