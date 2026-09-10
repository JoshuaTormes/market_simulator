// Trigger reference: the ledger's average cost when it agrees with the position
// this agent believes it holds, otherwise the price recorded at entry.  The two
// only disagree in the tick or two between a market entry and its fill.
#include "StopLossCluster.h"
#include <algorithm>
#include <cmath>

StopLossCluster::StopLossCluster(AgentId id, const std::string& ticker,
                                 std::mt19937_64 rng, Params p,
                                 RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , side_coin_(0.5)
    , qty_dist_(static_cast<long long>(std::max<Qty>(p.qty_min, 1)),
                static_cast<long long>(std::max<Qty>(p.qty_max, p.qty_min)))
    , side_(p.initial_side)
    , entry_ref_(static_cast<double>(p.entry_price))
    , qty_(p.qty)
{}

std::vector<Action> StopLossCluster::on_market_data(const AgentSnapshot& snap) {
    if (snap.base.spread < 0) return {};
    const Tick   now = snap.base.tick;
    const double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    // ── Flat: wait out the cooldown, then buy or sell into a new position ────
    if (!armed_) {
        if (now < cooldown_until_) return {};
        side_      = side_coin_(rng_) ? Side::Buy : Side::Sell;
        qty_       = static_cast<Qty>(qty_dist_(rng_));
        entry_ref_ = mid;              // refined below once the fill lands
        armed_     = true;
        return { submit(side_, OrderType::Market, 0, qty_) };
    }

    // ── Armed: measure the loss against the best reference available ─────────
    const bool is_long = (side_ == Side::Buy);
    const bool ledger_agrees =
        snap.own_avg_cost > 0.0 &&
        ((is_long && snap.own_inventory > 0.0) || (!is_long && snap.own_inventory < 0.0));
    const double ref = ledger_agrees ? snap.own_avg_cost : entry_ref_;

    const bool breached = is_long ? (mid < ref * (1.0 - p_.trigger_pct))
                                  : (mid > ref * (1.0 + p_.trigger_pct));
    if (!breached) return {};

    Qty liq_qty = static_cast<Qty>(std::llround(std::abs(snap.own_inventory)));
    if (liq_qty <= 0) liq_qty = qty_;

    armed_          = false;
    cooldown_until_ = now + static_cast<Tick>(std::max(1, p_.cooldown_ticks));
    ++stops_fired_;

    const Side exit = is_long ? Side::Sell : Side::Buy;
    return { submit(exit, OrderType::Market, 0, liq_qty) };
}
