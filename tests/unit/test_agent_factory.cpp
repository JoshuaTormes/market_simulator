#include <catch_amalgamated.hpp>
#include "agents/AgentFactory.h"
#include "core/RngService.h"
#include <map>
#include <string>

TEST_CASE("AgentFactory: correct count of each type", "[factory]") {
    PopulationConfig cfg;
    cfg.market_makers.count   = 2;
    cfg.noise_traders.count   = 5;
    cfg.informed_traders.count = 1;
    cfg.momentum_traders.count = 2;
    cfg.mean_reverters.count  = 2;
    cfg.value_investors.count = 1;
    cfg.institutionals.count  = 1;
    cfg.stop_loss.count       = 3;
    cfg.news_reactors.count   = 2;

    RngService rng(42);
    AgentFactory factory(cfg, rng, "T");
    auto agents = factory.create_all();

    int expected_total = 2 + 5 + 1 + 2 + 2 + 1 + 1 + 3 + 2;
    CHECK(static_cast<int>(agents.size()) == expected_total);

    std::map<std::string, int> counts;
    for (const auto& a : agents)
        ++counts[a->type()];

    CHECK(counts["MarketMakerAS"]        == 2);
    CHECK(counts["NoiseTrader"]          == 5);
    CHECK(counts["InformedTraderKyle"]   == 1);
    CHECK(counts["MomentumTrader"]       == 2);
    CHECK(counts["MeanReverterOU"]       == 2);
    CHECK(counts["ValueInvestor"]        == 1);
    CHECK(counts["InstitutionalExecutor"] == 1);
    CHECK(counts["StopLossCluster"]      == 3);
    CHECK(counts["NewsReactor"]          == 2);
}

TEST_CASE("AgentFactory: same seed → same agent IDs and types", "[factory][determinism]") {
    PopulationConfig cfg;
    cfg.noise_traders.count = 3;
    cfg.market_makers.count = 2;

    RngService rng1(7), rng2(7);
    AgentFactory f1(cfg, rng1, "T");
    AgentFactory f2(cfg, rng2, "T");

    auto a1 = f1.create_all();
    auto a2 = f2.create_all();

    REQUIRE(a1.size() == a2.size());
    for (size_t i = 0; i < a1.size(); ++i) {
        CHECK(a1[i]->id()   == a2[i]->id());
        CHECK(std::string(a1[i]->type()) == std::string(a2[i]->type()));
    }
}

TEST_CASE("AgentFactory: agents have unique IDs", "[factory]") {
    PopulationConfig cfg;
    cfg.noise_traders.count = 10;
    cfg.market_makers.count = 3;

    RngService rng(42);
    AgentFactory factory(cfg, rng, "T");
    auto agents = factory.create_all();

    std::map<AgentId, int> id_counts;
    for (const auto& a : agents)
        ++id_counts[a->id()];

    for (const auto& [id, cnt] : id_counts)
        CHECK(cnt == 1);
}

TEST_CASE("AgentFactory: only InformedTraderKyle sees fundamental", "[factory]") {
    PopulationConfig cfg;
    cfg.informed_traders.count  = 2;
    cfg.value_investors.count   = 1;
    cfg.market_makers.count     = 1;

    RngService rng(42);
    AgentFactory factory(cfg, rng, "T");
    auto agents = factory.create_all();

    for (const auto& a : agents) {
        std::string t = a->type();
        if (t == "InformedTraderKyle") {
            CHECK(a->info().sees_fundamental);  // only informed traders have fundamental access
        } else {
            CHECK_FALSE(a->info().sees_fundamental);  // MM and VI no longer see fundamental
        }
    }
}

TEST_CASE("AgentFactory: position limits come from the config", "[factory]") {
    // A limit left at the 1000-lot default is what pinned the noise cohort
    // against the risk gate; the factory must pass the configured one through.
    PopulationConfig cfg;
    cfg.market_makers.count    = 0;
    cfg.informed_traders.count = 0;
    cfg.momentum_traders.count = 0;
    cfg.mean_reverters.count   = 0;
    cfg.value_investors.count  = 0;
    cfg.stop_loss.count        = 0;
    cfg.noise_traders.count    = 2;
    cfg.institutionals.count   = 1;
    cfg.news_reactors.count    = 1;

    cfg.noise_traders.max_position  = 5000;
    cfg.institutionals.max_position = 4000;
    cfg.news_reactors.max_position  = 2000;

    RngService rng(42);
    AgentFactory factory(cfg, rng, "T");
    auto agents = factory.create_all();

    for (const auto& a : agents) {
        std::string t = a->type();
        if      (t == "NoiseTrader")           CHECK(a->risk().max_position == 5000);
        else if (t == "InstitutionalExecutor") CHECK(a->risk().max_position == 4000);
        else if (t == "NewsReactor")           CHECK(a->risk().max_position == 2000);
    }
}

TEST_CASE("AgentFactory: the default ecology pairs momentum against reversion",
          "[factory]") {
    // Trend followers alone are one-sided herding; the mean reverters are the
    // counterparty that keeps the return ACF near zero.
    PopulationConfig cfg;
    CHECK(cfg.momentum_traders.count == 3);
    CHECK(cfg.mean_reverters.count   == 2);
    CHECK(cfg.institutionals.count   == 2);
}
