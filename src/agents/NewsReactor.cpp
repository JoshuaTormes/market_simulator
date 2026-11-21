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
            const double ref = (p_.impact_scale > 0.0) ? p_.impact_scale : 1.0;
            double scale = std::min(std::abs(perceived_impact) / ref
                                        * static_cast<double>(p_.base_qty),
                                    static_cast<double>(rl_.max_position));
            Qty qty = std::max(Qty{1}, static_cast<Qty>(std::round(scale)));

            // One reaction, at one tick, dispersed inside the event window.
            std::uniform_real_distribution<double> spread_u(
                0.0, std::max(1.0, ev.duration_ticks));
            const Tick fire = start + static_cast<Tick>(std::floor(spread_u(rng_)));
            queue_.push_back({ fire, side, qty, false });
            if (p_.hold_max_ticks > 0.0) {
                const double lo = std::max(1.0, p_.hold_min_ticks);
                const double hi = std::max(lo, p_.hold_max_ticks);
                std::uniform_real_distribution<double> hold_u(std::log(lo), std::log(hi));
                const Tick hold = static_cast<Tick>(std::llround(std::exp(hold_u(rng_))));
                queue_.push_back({ fire + hold, opposite(side), qty, true });
            }
        });
    }
}

std::vector<Action> NewsReactor::on_market_data(const AgentSnapshot& snap) {
    Tick now = snap.base.tick;
    std::vector<Action> actions;

    auto it = queue_.begin();
    while (it != queue_.end()) {
        if (it->fire_tick <= now) {
            Qty q = it->qty;
            if (it->unwind) {
                // Close only what is actually held on that side: an entry the
                // risk gate rejected must not turn into an opposite position.
                const double inv  = snap.own_inventory;
                const bool   flat = (it->side == Side::Sell) ? inv <= 0.0 : inv >= 0.0;
                q = flat ? 0 : std::min<Qty>(q, static_cast<Qty>(std::llround(std::abs(inv))));
            }
            if (q > 0) actions.push_back(submit(it->side, OrderType::Market, 0, q));
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }
    return actions;
}
