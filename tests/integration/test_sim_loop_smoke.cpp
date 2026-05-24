// Smoke test: run SimulationLoop for 500 ticks with minimal population.
// Verifies: no assert/crash, conservation invariant (total_net_qty == 0).
#include <catch_amalgamated.hpp>
#include "sim/SimulationLoop.h"
#include "core/RngService.h"
#include "core/EventBus.h"
#include "core/Logger.h"
#include "matching/MatchingEngine.h"
#include "ledger/PositionLedger.h"
#include "clearing/Clearing.h"
#include "marketdata/MarketDataPublisher.h"
#include "marketdata/SnapshotBuffer.h"
#include "economics/GBMProcess.h"
#include "economics/PoissonNewsProcess.h"
#include "agents/AgentFactory.h"
#include "agents/AgentRunner.h"
#include "risk/RiskGate.h"
#include "orderbook/Order.h"
#include <memory>
#include <vector>
#include <string>

static void seed_book(MatchingEngine& eng, const std::string& ticker,
                      Price mid, Qty qty, Tick now) {
    // Place resting bid and ask at mid ± 5 from agent 0
    for (int spread : {-5, 5}) {
        Order o;
        o.id           = static_cast<OrderId>(100 + spread);
        o.agent_id     = 0;
        o.side         = (spread < 0) ? Side::Buy : Side::Sell;
        o.type         = OrderType::Limit;
        o.price        = mid + spread;
        o.qty          = qty;
        o.submit_tick  = now;
        o.seq_no       = eng.next_seq();
        o.ticker       = ticker;
        eng.submit(o, now, LatencyProfile{});
    }
    eng.process_until(now, [](const Trade&){});
}

TEST_CASE("SimulationLoop smoke: 500 ticks, no crash, conservation holds", "[smoke]") {
    const std::string TICKER = "TEST";
    RngService rng(42);
    EventBus   bus;

    MatchingEngine   matching(TICKER, FeeModel{}, STPMode::CancelBoth, &rng);
    PositionLedger   ledger;
    Clearing         clearing(ledger, TICKER);
    MarketDataPublisher publisher(matching.book());

    // Seed book so agents have a reference price
    seed_book(matching, TICKER, 10000, 100, 0);

    GBMProcess::Config gbm_cfg{ .s0=100.0, .mu=0.0001, .sigma=0.01 };
    GBMProcess fundamental(gbm_cfg, rng.for_consumer("fundamental"));

    PoissonNewsProcess::Config news_cfg;
    news_cfg.lambda = 0.001;
    news_cfg.ticker = TICKER;
    PoissonNewsProcess news(news_cfg, rng.for_consumer("news"), &bus);

    // Minimal population: 1 market maker + 3 noise traders
    PopulationConfig pop;
    pop.market_makers.count  = 1;
    pop.noise_traders.count  = 3;
    pop.informed_traders.count = 0;
    pop.momentum_traders.count = 0;
    pop.mean_reverters.count   = 0;
    pop.value_investors.count  = 0;
    pop.institutionals.count   = 0;
    pop.stop_loss.count        = 0;
    pop.news_reactors.count    = 0;

    AgentFactory factory(pop, rng, TICKER, &bus);
    auto owned = factory.create_all();
    std::vector<IAgent*> ptrs;
    for (auto& a : owned) ptrs.push_back(a.get());

    AgentRunner runner(ptrs, 42u);
    RiskGate    risk_gate(ledger, TICKER);

    Logger logger;  // no-op sink

    SimulationLoop::Config sim_cfg;
    sim_cfg.max_ticks        = 500;
    sim_cfg.publish_interval = 1;

    SnapshotBuffer snap_buf;
    SimulationLoop sim(sim_cfg, fundamental, news, runner, risk_gate,
                       matching, clearing, ledger, publisher, logger);

    // Run synchronously on test thread (no separate thread needed for smoke test).
    REQUIRE_NOTHROW(sim.run(snap_buf));

    // Conservation: sum of all net positions == 0 (no external shares introduced)
    Qty total = ledger.total_net_qty(TICKER);
    CHECK(total == 0);

    // Snapshot buffer has at least one entry
    CHECK(snap_buf.has_snapshot());

    // Last tick matches
    MarketSnapshot snap;
    REQUIRE(snap_buf.get_latest(snap));
    CHECK(snap.tick < 500);  // tick is 0-indexed up to 499
}

TEST_CASE("SimulationLoop smoke: deterministic — same seed same result", "[smoke][determinism]") {
    const std::string TICKER = "DET";

    auto run_sim = [&](uint64_t seed) -> Price {
        RngService rng(seed);
        EventBus bus;

        MatchingEngine   matching(TICKER, FeeModel{}, STPMode::CancelBoth, &rng);
        PositionLedger   ledger;
        Clearing         clearing(ledger, TICKER);
        MarketDataPublisher publisher(matching.book());

        seed_book(matching, TICKER, 10000, 100, 0);

        GBMProcess::Config gbm_cfg{ .s0=100.0, .mu=0.0, .sigma=0.01 };
        GBMProcess fundamental(gbm_cfg, rng.for_consumer("fundamental"));

        PoissonNewsProcess::Config ncfg;
        ncfg.lambda = 0.005; ncfg.ticker = TICKER;
        PoissonNewsProcess news(ncfg, rng.for_consumer("news"), &bus);

        PopulationConfig pop;
        pop.noise_traders.count = 2;
        pop.market_makers.count = 1;
        AgentFactory factory(pop, rng, TICKER, &bus);
        auto owned = factory.create_all();
        std::vector<IAgent*> ptrs;
        for (auto& a : owned) ptrs.push_back(a.get());

        AgentRunner runner(ptrs, 42u);
        RiskGate    risk_gate(ledger, TICKER);
        Logger      logger;

        SimulationLoop::Config scfg;
        scfg.max_ticks = 200;
        SnapshotBuffer snap_buf;
        SimulationLoop sim(scfg, fundamental, news, runner, risk_gate,
                           matching, clearing, ledger, publisher, logger);
        sim.run(snap_buf);

        MarketSnapshot snap;
        snap_buf.get_latest(snap);
        return snap.mid_price;
    };

    Price p1 = run_sim(7);
    Price p2 = run_sim(7);
    CHECK(p1 == p2);
}
