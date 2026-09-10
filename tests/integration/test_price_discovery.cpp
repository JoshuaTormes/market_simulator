// Price discovery in a minimal market: 1 market maker + 1 informed trader +
// 5 noise traders.  The question this answers is the one the stylized facts
// take for granted — does the traded mid follow the latent fundamental at all?
//
// The market maker never sees the fundamental; only the informed trader does,
// and only through noise.  So any correlation between the fundamental's
// returns and the mid's returns has to come through the order flow.
#include <catch_amalgamated.hpp>
#include "sim/SimulationLoop.h"
#include "core/RngService.h"
#include "core/EventBus.h"
#include "core/Logger.h"
#include "core/Config.h"
#include "matching/MatchingEngine.h"
#include "ledger/PositionLedger.h"
#include "clearing/Clearing.h"
#include "marketdata/MarketDataPublisher.h"
#include "marketdata/SnapshotBuffer.h"
#include "economics/FundamentalProcessFactory.h"
#include "economics/PoissonNewsProcess.h"
#include "agents/AgentFactory.h"
#include "agents/AgentRunner.h"
#include "risk/RiskGate.h"
#include "persistence/BinaryLogWriter.h"
#include "analysis/Report.h"
#include <cstdio>
#include <string>
#include <cmath>
#include <memory>
#include <vector>

namespace {

// Runs the mini market into a binary log and returns the price-discovery
// section of the standard report, so the test measures exactly what
// sim_headless prints rather than a second implementation of the same maths.
AnalysisReport run_mini_market(uint64_t seed, Tick ticks, const std::string& log_path) {
    const std::string TICKER = "PD";
    RngService rng(seed);
    EventBus   bus;

    MatchingEngine      matching(TICKER, FeeModel{}, STPMode::CancelBoth, &rng);
    PositionLedger      ledger;
    Clearing            clearing(ledger, TICKER);
    MarketDataPublisher publisher(matching.book());
    publisher.set_initial_mid(10000);

    SimulationConfig cfg;
    cfg.fundamental.type          = FundamentalProcessType::GBM;
    cfg.fundamental.initial_value = 100.0;
    // The calibrated 30% annual, deliberately: a test that only passes with an
    // inflated volatility is not testing the configuration that ships.
    auto fundamental_ptr = make_fundamental_process(cfg.fundamental, cfg.time,
                                                    rng.for_consumer("fundamental"));

    PoissonNewsProcess::Config ncfg;
    ncfg.lambda = 0.0;              // isolate the informed channel
    ncfg.ticker = TICKER;
    PoissonNewsProcess news(ncfg, rng.for_consumer("news"), &bus);

    PopulationConfig pop;
    pop.market_makers.count    = 1;
    pop.informed_traders.count = 3;
    pop.noise_traders.count    = 5;
    pop.momentum_traders.count = 0;
    pop.mean_reverters.count   = 0;
    pop.value_investors.count  = 0;
    pop.institutionals.count   = 0;
    pop.stop_loss.count        = 0;
    pop.news_reactors.count    = 0;

    AgentFactory factory(pop, rng, TICKER, &bus, &ledger);
    auto owned = factory.create_all();
    std::vector<IAgent*> ptrs;
    std::vector<AgentId> mm_ids;
    for (auto& a : owned) {
        ptrs.push_back(a.get());
        if (std::string(a->type()) == "MarketMakerAS") mm_ids.push_back(a->id());
    }

    AgentRunner runner(ptrs, rng.global_seed(), &ledger, TICKER);
    RiskGate    risk_gate(ledger, TICKER);
    Logger      logger;

    SimulationLoop::Config sim_cfg;
    sim_cfg.max_ticks        = ticks;
    sim_cfg.publish_interval = 1;
    sim_cfg.ticker           = TICKER;

    BinaryLogWriter writer(log_path, seed, ticks, 1, TICKER);
    SnapshotBuffer  snap_buf;
    SimulationLoop  sim(sim_cfg, *fundamental_ptr, news, runner, risk_gate,
                        matching, clearing, ledger, publisher, logger);
    sim.set_event_bus(&bus);
    sim.set_log_writer(&writer);

    sim.run(snap_buf);
    writer.flush(true);

    return Report::run(log_path, mm_ids);
}

}  // namespace

TEST_CASE("Price discovery: the mid tracks the fundamental", "[price_discovery]") {
    const std::string log = "test_price_discovery_42.bin";
    AnalysisReport rep = run_mini_market(42, 10000, log);
    std::remove(log.c_str());

    REQUIRE(rep.n_snapshots > 9000);
    REQUIRE(rep.pd.has_fundamental);

    // The book stays two-sided: a maker holding one side cannot price.  This
    // bar is well below the full-population target because a single maker
    // quoting 100 lots per side is emptied outright by one large market order
    // and cannot requote until the next tick.
    CHECK(rep.pd.two_sided_frac > 0.80);

    // The mid must move with the fundamental over a horizon long enough for
    // the informed flow to be absorbed.  In a market with one maker the 20-tick
    // correlation swings by a few hundredths from seed to seed, so the tighter
    // claim is made at h=100, where the estimate is stable.
    CHECK(rep.pd.corr_h20  > 0.45);
    CHECK(rep.pd.corr_h100 > 0.60);

    // The pricing error stays small, and shocks to it decay in tens of ticks
    // rather than persisting for the whole run.
    CHECK(rep.pd.gap_std < 0.01);
    CHECK(rep.pd.gap_half_life > 0.0);
    CHECK(rep.pd.gap_half_life < 100.0);
}

TEST_CASE("Price discovery: holds on a second seed", "[price_discovery]") {
    // An informed trader stuck at its position limit has stopped discovering
    // anything; the unwind leg is what keeps it able to act on the next
    // signal.  Repeating on another seed catches a result that only holds for
    // one particular path.
    const std::string log = "test_price_discovery_7.bin";
    AnalysisReport rep = run_mini_market(7, 10000, log);
    std::remove(log.c_str());

    CHECK(rep.pd.two_sided_frac > 0.80);
    CHECK(rep.pd.corr_h20 > 0.4);
    CHECK(rep.pd.gap_std < 0.01);
}
