#pragma once
// Reacts to news events with heterogeneous latency and idiosyncratic magnitude scaling.
// Motivated by Tetlock (2007) and the news-driven return model of Chan (2003).
#include "AgentBase.h"
#include "core/EventBus.h"
#include "economics/NewsEvent.h"
#include <random>
#include <vector>

struct PendingReaction {
    Tick   fire_tick;
    Side   side;
    Qty    qty;
};

class NewsReactor : public AgentBase {
public:
    struct Params {
        double reaction_lag_mean  = 3.0;   // mean ticks before reacting (exponential)
        double sensitivity        = 1.0;   // multiplier on news impact_log_return
        Qty    base_qty           = 20;    // lots for an impact of exactly impact_scale
        // Reference impact the size is quoted against.  Announcement impacts are
        // O(0.4%), so scaling qty by the raw log-return collapsed every reaction
        // to the 1-lot floor and the reactors never moved the price.  Dividing
        // by the scale makes base_qty the size of a *typical* headline and lets
        // the Student-t tail produce the rare very large reaction.
        double impact_scale       = 0.004;
        double dispersion_sigma   = 0.3;   // idiosyncratic noise on perceived impact
    };

    NewsReactor(AgentId id, const std::string& ticker,
                std::mt19937_64 rng, Params p, EventBus* bus,
                RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "NewsReactor"; }

private:
    Params p_;
    std::exponential_distribution<double> lag_dist_;
    std::normal_distribution<double>      dispersion_noise_;
    std::vector<PendingReaction>          queue_;
};
