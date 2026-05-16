// Replay determinism: same seed + config must produce bit-identical binary logs.
// Cross-architecture determinism is NOT guaranteed (libm may differ between
// x86 and ARM for transcendental functions), so this test validates same-arch only.
#include <catch_amalgamated.hpp>
#include "persistence/BinaryLogWriter.h"
#include "persistence/Replay.h"
#include "sim/SimulationLoop.h"
#include "core/RngService.h"
#include "core/EventBus.h"
#include "core/Logger.h"
#include "orderbook/OrderBookV2.h"
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
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

static std::string tmp(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// Run a short simulation, write output to log_path, return last mid_price.
static Price run_sim(uint64_t seed, const std::string& log_path) {
    const std::string TICKER = "DET";
    const uint64_t    TICKS  = 300;

    RngService rng(seed);
    EventBus   bus;

    OrderBookV2         book;
    MatchingEngine      matching(TICKER, FeeModel{}, STPMode::CancelBoth, &rng);
    PositionLedger      ledger;
    Clearing            clearing(ledger, TICKER);
    MarketDataPublisher publisher(book);

    // Seed book with resting bid/ask so agents have a reference price.
    {
        for (int sp : {-5, 5}) {
            Order o;
            o.id          = static_cast<OrderId>(1000 + sp);
            o.agent_id    = 0;
            o.side        = (sp < 0) ? Side::Buy : Side::Sell;
            o.type        = OrderType::Limit;
            o.price       = 10000 + sp;
            o.qty         = 200;
            o.submit_tick = 0;
            o.seq_no      = matching.next_seq();
            o.ticker      = TICKER;
            matching.submit(o, 0, LatencyProfile{});
        }
        matching.process_until(0, [](const Trade&){});
    }

    GBMProcess::Config gbm{ .s0=100.0, .mu=0.0, .sigma=0.01 };
    GBMProcess fundamental(gbm, rng.for_consumer("fundamental"));

    PoissonNewsProcess::Config ncfg;
    ncfg.lambda = 0.005; ncfg.ticker = TICKER;
    PoissonNewsProcess news(ncfg, rng.for_consumer("news"), &bus);

    PopulationConfig pop;
    pop.market_makers.count   = 1;
    pop.noise_traders.count   = 3;
    pop.informed_traders.count = 0;
    pop.momentum_traders.count = 0;
    pop.mean_reverters.count  = 0;
    pop.value_investors.count = 0;
    pop.institutionals.count  = 0;
    pop.stop_loss.count       = 0;
    pop.news_reactors.count   = 0;

    AgentFactory factory(pop, rng, TICKER, &bus);
    auto owned = factory.create_all();
    std::vector<IAgent*> ptrs;
    for (auto& a : owned) ptrs.push_back(a.get());

    AgentRunner runner(ptrs);
    RiskGate    risk_gate(ledger, TICKER);
    Logger      logger;

    SimulationLoop::Config scfg;
    scfg.max_ticks        = TICKS;
    scfg.publish_interval = 1;
    scfg.ticker           = TICKER;

    BinaryLogWriter log_writer(log_path, seed, TICKS, 1, TICKER);

    SnapshotBuffer snap_buf;
    SimulationLoop sim(scfg, fundamental, news, runner, risk_gate,
                       matching, clearing, ledger, publisher, logger);
    sim.set_event_bus(&bus);
    sim.set_log_writer(&log_writer);

    sim.run(snap_buf);
    log_writer.flush(false);

    MarketSnapshot snap;
    snap_buf.get_latest(snap);
    return snap.mid_price;
}

TEST_CASE("Replay: same seed produces identical binary logs", "[replay][determinism]") {
    const uint64_t SEED = 42;
    std::string log_a = tmp("det_a.bin");
    std::string log_b = tmp("det_b.bin");

    Price p1 = run_sim(SEED, log_a);
    Price p2 = run_sim(SEED, log_b);

    // Same final price is a necessary but not sufficient condition.
    CHECK(p1 == p2);

    // Bit-identical logs is the strong guarantee.
    CHECK(Replay::logs_identical(log_a, log_b));
}

TEST_CASE("Replay: different seeds produce different logs", "[replay][determinism]") {
    std::string log_a = tmp("diff_a.bin");
    std::string log_b = tmp("diff_b.bin");

    run_sim(1, log_a);
    run_sim(2, log_b);

    // Extremely unlikely to collide; statistically guaranteed to differ.
    CHECK_FALSE(Replay::logs_identical(log_a, log_b));
}

TEST_CASE("Replay: load_config reads seed and max_ticks from log", "[replay]") {
    std::string log_path = tmp("cfg_read.bin");
    run_sim(77, log_path);

    SimulationConfig cfg;
    REQUIRE(Replay::load_config(log_path, cfg));
    CHECK(cfg.seed      == 77);
    CHECK(cfg.max_ticks == 300);
}

TEST_CASE("Replay: count_records returns non-zero after sim run", "[replay]") {
    std::string log_path = tmp("cnt.bin");
    run_sim(13, log_path);

    auto stats = Replay::count_records(log_path);
    CHECK(stats.snapshots > 0);   // at least one snapshot per tick
    CHECK(stats.total_records > 0);
}
