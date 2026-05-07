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

    if (position_ == 0) {
        if (z < -p_.entry_z)
            actions.push_back(submit(Side::Buy,  OrderType::Market, 0, p_.qty)), position_ =  1;
        else if (z > p_.entry_z)
            actions.push_back(submit(Side::Sell, OrderType::Market, 0, p_.qty)), position_ = -1;
    } else if (position_ > 0 && z > -p_.exit_z) {
        actions.push_back(submit(Side::Sell, OrderType::Market, 0, p_.qty));
        position_ = 0;
    } else if (position_ < 0 && z < p_.exit_z) {
        actions.push_back(submit(Side::Buy,  OrderType::Market, 0, p_.qty));
        position_ = 0;
    }

    return actions;
}
