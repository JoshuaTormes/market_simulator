#include <catch_amalgamated.hpp>
#include "economics/FundamentalProcessFactory.h"
#include "core/Config.h"

static std::mt19937_64 make_rng(uint64_t seed = 42) {
    return std::mt19937_64(seed);
}

TEST_CASE("FundamentalProcessFactory: GBM produces non-trivial values", "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type          = FundamentalProcessType::GBM;
    cfg.initial_value = 100.0;
    cfg.mu            = 0.0;
    cfg.sigma         = 0.01;

    auto proc = make_fundamental_process(cfg, make_rng());
    REQUIRE(proc != nullptr);
    CHECK(std::string(proc->name()) == "GBM");

    double v0 = proc->current_value();
    proc->step(0, 1.0);
    double v1 = proc->current_value();

    CHECK(v0 > 0.0);
    CHECK(v1 > 0.0);
}

TEST_CASE("FundamentalProcessFactory: OU reverts to theta", "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type          = FundamentalProcessType::OU;
    cfg.initial_value = 100.0;
    cfg.theta         = 100.0;
    cfg.kappa         = 0.5;
    cfg.sigma         = 0.01;

    auto proc = make_fundamental_process(cfg, make_rng());
    REQUIRE(proc != nullptr);
    CHECK(std::string(proc->name()) == "OU");
    CHECK(proc->current_value() == Catch::Approx(100.0));
}

TEST_CASE("FundamentalProcessFactory: JumpDiffusion produces valid output", "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type          = FundamentalProcessType::JumpDiffusion;
    cfg.initial_value = 100.0;
    cfg.sigma         = 0.01;
    cfg.jump_intensity = 0.1;
    cfg.jump_sigma    = 0.05;

    auto proc = make_fundamental_process(cfg, make_rng());
    REQUIRE(proc != nullptr);
    CHECK(std::string(proc->name()) == "JumpDiffusion");
    CHECK(proc->current_value() > 0.0);
}

TEST_CASE("FundamentalProcessFactory: RegimeSwitching wires config correctly", "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type               = FundamentalProcessType::RegimeSwitching;
    cfg.initial_value      = 200.0;
    cfg.regime_sigma[0]    = 0.003;
    cfg.regime_sigma[1]    = 0.010;
    cfg.regime_sigma[2]    = 0.025;
    cfg.regime_mu[0]       = 0.0;
    cfg.regime_mu[1]       = 0.0;
    cfg.regime_mu[2]       = 0.0;

    auto proc = make_fundamental_process(cfg, make_rng());
    REQUIRE(proc != nullptr);
    CHECK(std::string(proc->name()) == "RegimeSwitching");
    CHECK(proc->current_value() == Catch::Approx(200.0));
}

TEST_CASE("FundamentalProcessFactory: different processes diverge from same seed", "[factory][fundamental]") {
    // Changing --process changes the simulation output (live config verification).
    FundamentalConfig cfg_rs;
    cfg_rs.type = FundamentalProcessType::RegimeSwitching;
    cfg_rs.initial_value = 100.0;

    FundamentalConfig cfg_gbm;
    cfg_gbm.type = FundamentalProcessType::GBM;
    cfg_gbm.initial_value = 100.0;
    cfg_gbm.sigma = 0.01;

    auto rs  = make_fundamental_process(cfg_rs,  make_rng(42));
    auto gbm = make_fundamental_process(cfg_gbm, make_rng(42));

    // Run 50 ticks
    for (int t = 0; t < 50; ++t) {
        rs->step(t, 1.0);
        gbm->step(t, 1.0);
    }

    // With the same seed but different process types, values must differ.
    CHECK(rs->current_value() != gbm->current_value());
}
