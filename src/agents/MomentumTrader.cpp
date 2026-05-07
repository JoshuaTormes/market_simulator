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

    // Stop loss check for open position
    if (position_ != 0 && entry_px_ > 0 && atr > 0.0) {
        double stop_dist = p_.atr_mult * atr;
        double ep = static_cast<double>(entry_px_);
        bool stop_hit = (position_ > 0 && mid < ep - stop_dist)
                     || (position_ < 0 && mid > ep + stop_dist);
        if (stop_hit) {
            Side exit_side = (position_ > 0) ? Side::Sell : Side::Buy;
            actions.push_back(submit(exit_side, OrderType::Market, 0, p_.qty));
            position_ = 0;
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

    if (is_long_now && !was_long && position_ <= 0) {
        if (position_ < 0)
            actions.push_back(submit(Side::Buy, OrderType::Market, 0, p_.qty));  // close short
        actions.push_back(submit(Side::Buy, OrderType::Market, 0, p_.qty));
        position_  = 1;
        entry_px_  = snap.perceived_mid;
    } else if (!is_long_now && was_long && position_ >= 0) {
        if (position_ > 0)
            actions.push_back(submit(Side::Sell, OrderType::Market, 0, p_.qty)); // close long
        actions.push_back(submit(Side::Sell, OrderType::Market, 0, p_.qty));
        position_  = -1;
        entry_px_  = snap.perceived_mid;
    }

    prev_fast_ = fast_now;
    prev_slow_ = slow_now;
    return actions;
}
