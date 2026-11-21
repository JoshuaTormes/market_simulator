// Enter long when z < -entry_z (cheap), short when z > +entry_z (expensive).
#include "MeanReverterOU.h"
#include <cmath>

MeanReverterOU::MeanReverterOU(AgentId id, const std::string& ticker,
                               std::mt19937_64 rng, Params p,
                               RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , win_(p.window)
{}

std::vector<Action> MeanReverterOU::on_market_data(const AgentSnapshot& snap) {
    if (snap.base.spread < 0) return {};

    double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    win_.push(mid);
    ++ticks_;
    if (ticks_ < p_.window) return {};

    double mu  = win_.mean();
    double sig = win_.stddev();
    if (sig < 1e-10) return {};

    double z = (mid - mu) / sig;
    std::vector<Action> actions;

    // Target position and current side both from the ledger, not a flag: see
    // MomentumTrader — an order lost to an empty book side must not become
    // permanent inventory, and a lost exit is retried on the next tick.
    const auto to_target = [&](int sign) {
        const double gap = sign * static_cast<double>(p_.qty) - snap.own_inventory;
        const Qty    q   = static_cast<Qty>(std::llround(std::abs(gap)));
        if (q > 0) actions.push_back(submit(gap > 0.0 ? Side::Buy : Side::Sell,
                                            OrderType::Market, 0, q));
    };
    const int position = snap.own_inventory > 0.0 ? 1 : (snap.own_inventory < 0.0 ? -1 : 0);

    if (position == 0) {
        if (z < -p_.entry_z)      to_target(+1);
        else if (z > p_.entry_z)  to_target(-1);
    } else if (position > 0 && z > -p_.exit_z) {
        to_target(0);
    } else if (position < 0 && z < p_.exit_z) {
        to_target(0);
    }

    return actions;
}
