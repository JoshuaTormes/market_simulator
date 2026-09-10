#include <catch_amalgamated.hpp>
#include "agents/NewsReactor.h"
#include "core/EventBus.h"
#include "core/RngService.h"
#include <cmath>

static AgentSnapshot snap_at(Tick t) {
    AgentSnapshot as;
    as.base.tick      = t;
    as.base.spread    = 2;
    as.base.mid_price = 10000;
    as.perceived_mid  = 10000;
    return as;
}

static NewsEvent event(double impact, Tick at = 0, double duration = 1.0) {
    NewsEvent ev;
    ev.announce_tick     = at;
    ev.ticker            = "T";
    ev.impact_log_return = impact;
    ev.duration_ticks    = duration;
    return ev;
}

// Total lots the reactor sends in response to one announcement.
static long long reaction_size(double impact, Qty base_qty, double impact_scale,
                               Qty max_position) {
    RngService rng(42);
    EventBus bus;
    NewsReactor::Params p;
    p.reaction_lag_mean = 0.0;
    p.sensitivity       = 1.0;
    p.base_qty          = base_qty;
    p.impact_scale      = impact_scale;
    p.dispersion_sigma  = 0.0;      // no idiosyncratic noise: exact arithmetic
    RiskLimits rl;
    rl.max_position = max_position;

    NewsReactor nr(1, "T", rng.for_consumer("nr"), p, &bus, rl);
    bus.publish(event(impact));

    long long total = 0;
    for (Tick t = 0; t < 5; ++t)
        for (const Action& a : nr.on_market_data(snap_at(t)))
            total += std::get<SubmitOrder>(a).qty;
    return total;
}

TEST_CASE("NewsReactor: size scales with impact relative to impact_scale",
          "[news_reactor]") {
    // Announcement impacts are O(0.4%).  Multiplying base_qty by the raw
    // log-return put every reaction on the 1-lot floor, so the reactors never
    // moved anything.  Quoting size against impact_scale makes base_qty the
    // reaction to a typical headline.
    CHECK(reaction_size(0.004, 50, 0.004, 2000) == 50);
    CHECK(reaction_size(0.008, 50, 0.004, 2000) == 100);
    CHECK(reaction_size(0.002, 50, 0.004, 2000) == 25);
}

TEST_CASE("NewsReactor: size is capped by the position limit", "[news_reactor]") {
    // A 20% headline is 50x a typical one; without the cap it would ask for
    // 2500 lots.
    CHECK(reaction_size(0.20, 50, 0.004, 300) == 300);
}

TEST_CASE("NewsReactor: side follows the sign of the impact", "[news_reactor]") {
    RngService rng(42);
    EventBus bus;
    NewsReactor::Params p;
    p.reaction_lag_mean = 0.0;
    p.base_qty          = 50;
    p.impact_scale      = 0.004;
    p.dispersion_sigma  = 0.0;

    NewsReactor nr(1, "T", rng.for_consumer("nr"), p, &bus);
    bus.publish(event(-0.01));
    auto acts = nr.on_market_data(snap_at(0));
    REQUIRE(acts.size() == 1);
    CHECK(std::get<SubmitOrder>(acts[0]).side == Side::Sell);
    CHECK(std::get<SubmitOrder>(acts[0]).type == OrderType::Market);
}
