// TWAP: child_qty = parent_qty / total_slices, submitted every ticks_between ticks.
#include "InstitutionalExecutor.h"
#include <algorithm>

InstitutionalExecutor::InstitutionalExecutor(AgentId id, const std::string& ticker,
                                             std::mt19937_64 rng, Params p,
                                             RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , remaining_(p.parent_qty)
{}

std::vector<Action> InstitutionalExecutor::on_market_data(const AgentSnapshot& snap) {
    if (remaining_ <= 0) return {};

    Tick now = snap.base.tick;
    if (!started_) {
        next_tick_ = now;
        started_   = true;
    }
    if (now < next_tick_) return {};

    int slices_left = p_.total_slices - slices_done_;
    if (slices_left <= 0) return {};

    Qty child_qty = remaining_ / slices_left;
    child_qty     = std::max(child_qty, Qty{1});
    child_qty     = std::min(child_qty, remaining_);

    remaining_    -= child_qty;
    ++slices_done_;
    next_tick_     = now + static_cast<Tick>(p_.ticks_between);

    return { submit(p_.side, OrderType::Market, 0, child_qty) };
}
