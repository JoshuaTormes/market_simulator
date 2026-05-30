#include "NewsReactor.h"
#include <cmath>
#include <algorithm>

NewsReactor::NewsReactor(AgentId id, const std::string& ticker,
                         std::mt19937_64 rng, Params p, EventBus* bus,
                         RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , lag_dist_(1.0 / std::max(p.reaction_lag_mean, 0.001))
    , dispersion_noise_(0.0, p.dispersion_sigma)
{
    if (bus) {
        bus->subscribe<NewsEvent>([this](const NewsEvent& ev) {
            double perceived_impact = ev.impact_log_return * p_.sensitivity
                                    * (1.0 + dispersion_noise_(rng_));
            if (std::abs(perceived_impact) < 1e-6) return;

            double lag  = lag_dist_(rng_);
            Tick start  = ev.announce_tick + static_cast<Tick>(std::round(lag));
            Side side   = (perceived_impact > 0.0) ? Side::Buy : Side::Sell;
            double scale = std::min(std::abs(perceived_impact) * static_cast<double>(p_.base_qty),
                                    static_cast<double>(rl_.max_position));
            Qty qty = std::max(Qty{1}, static_cast<Qty>(std::round(scale)));

            // Fire once per tick for the full event duration — creates sustained
            // directional OFI that drives vol clustering (Facts 3 & 4).
            int n_ticks = std::max(1, static_cast<int>(std::round(ev.duration_ticks)));
            for (int dt = 0; dt < n_ticks; ++dt)
                queue_.push_back({ start + static_cast<Tick>(dt), side, qty });
        });
    }
}

std::vector<Action> NewsReactor::on_market_data(const AgentSnapshot& snap) {
    Tick now = snap.base.tick;
    std::vector<Action> actions;

    auto it = queue_.begin();
    while (it != queue_.end()) {
        if (it->fire_tick <= now) {
            actions.push_back(submit(it->side, OrderType::Market, 0, it->qty));
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    return actions;
}
