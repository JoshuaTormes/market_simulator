// Dual-MA crossover with ATR trailing stop. Long when fast > slow, short otherwise.
#include "MomentumTrader.h"
#include <cmath>

MomentumTrader::MomentumTrader(AgentId id, const std::string& ticker,
                                std::mt19937_64 rng, Params p,
                                RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , fast_ma_(p.fast_window)
    , slow_ma_(p.slow_window)
    , atr_win_(p.atr_window)
{}

std::vector<Action> MomentumTrader::on_market_data(const AgentSnapshot& snap) {
    if (snap.base.spread < 0) return {};

    double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    fast_ma_.push(mid);
    slow_ma_.push(mid);

    // Approximate ATR as realized_vol[0] * mid (short window)
    double vol_est = snap.base.realized_vol[0];
    atr_win_.push(vol_est * mid);

    ++ticks_seen_;
    if (ticks_seen_ < p_.slow_window) return {};

    double fast_now = fast_ma_.mean();
    double slow_now = slow_ma_.mean();
    double atr      = atr_win_.mean();

    std::vector<Action> actions;

    // Orders go to a target position measured against the ledger, not against
    // the internal flag: a market order that meets an empty side of the book
    // is lost, and counting it as filled let the real inventory random-walk
    // to the hard limit (≈500 lots per trader over 60 sessions with qty = 10).
    const auto to_target = [&](int sign) {
        const double gap = sign * static_cast<double>(p_.qty) - snap.own_inventory;
        const Qty    q   = static_cast<Qty>(std::llround(std::abs(gap)));
        if (q > 0) actions.push_back(submit(gap > 0.0 ? Side::Buy : Side::Sell,
                                            OrderType::Market, 0, q));
    };

    // Side and entry price come from the ledger too.  The ledger resets the
    // cost basis when the position crosses zero, so own_avg_cost is the price
    // of the position actually held; a stop whose exit order is lost fires
    // again next tick instead of being forgotten by a flag.
    const int    position = snap.own_inventory > 0.0 ? 1 : (snap.own_inventory < 0.0 ? -1 : 0);
    const double ep       = snap.own_avg_cost;

    // Stop loss check for open position
    if (position != 0 && ep > 0.0 && atr > 0.0) {
        double stop_dist = p_.atr_mult * atr;
        bool stop_hit = (position > 0 && mid < ep - stop_dist)
                     || (position < 0 && mid > ep + stop_dist);
        if (stop_hit) {
            to_target(0);
            prev_fast_ = fast_now;
            prev_slow_ = slow_now;
            return actions;
        }
    }

    // Signal: fast crosses slow
    bool was_long   = (prev_fast_ > prev_slow_);
    bool is_long_now = (fast_now  > slow_now);

    if (!warm_) {
        prev_fast_ = fast_now;
        prev_slow_ = slow_now;
        warm_ = true;
        return {};
    }

    if (is_long_now && !was_long && position <= 0) {
        to_target(+1);
    } else if (!is_long_now && was_long && position >= 0) {
        to_target(-1);
    }

    prev_fast_ = fast_now;
    prev_slow_ = slow_now;
    return actions;
}
