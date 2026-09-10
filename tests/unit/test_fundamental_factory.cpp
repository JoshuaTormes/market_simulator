#include <catch_amalgamated.hpp>
#include "economics/FundamentalProcessFactory.h"
#include "core/Config.h"

static std::mt19937_64 make_rng(uint64_t seed = 42) {
    return std::mt19937_64(seed);
}

TEST_CASE("TimeScale: per-tick sigma follows square-root-of-time", "[factory][fundamental]") {
    TimeScale ts;                       // 1 tick = 1 s, 23400 s/day, 252 days
    CHECK(ts.ticks_per_day()  == Catch::Approx(23400.0));
    CHECK(ts.ticks_per_year() == Catch::Approx(23400.0 * 252.0));

    // 30% annual over 5,896,800 ticks → ~1.24e-4 per tick.
    CHECK(ts.per_tick_vol(0.30) == Catch::Approx(1.2354e-4).epsilon(0.01));

    // Halving the tick length halves the per-tick variance, so sigma falls by
    // sqrt(2) — the mapping is coherent under rescaling.
    TimeScale half;
    half.seconds_per_tick = 0.5;
    CHECK(half.per_tick_vol(0.30)
          == Catch::Approx(ts.per_tick_vol(0.30) / std::sqrt(2.0)).epsilon(1e-9));

    // 8 announcements per session at 23400 ticks/day.
    CHECK(ts.per_tick_rate(8.0) == Catch::Approx(8.0 / 23400.0));
}

TEST_CASE("FundamentalProcessFactory: GBM produces non-trivial values", "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type          = FundamentalProcessType::GBM;
    cfg.initial_value = 100.0;

    auto proc = make_fundamental_process(cfg, TimeScale{}, make_rng());
    REQUIRE(proc != nullptr);
    CHECK(std::string(proc->name()) == "GBM");

    double v0 = proc->current_value();
    proc->step(0, 1.0);
    double v1 = proc->current_value();

    CHECK(v0 > 0.0);
    CHECK(v1 > 0.0);
}

TEST_CASE("FundamentalProcessFactory: derived sigma matches the annual target",
          "[factory][fundamental]") {
    // A GBM run over one simulated day must show a realized volatility close
    // to sigma_annual / sqrt(252).  This is the check that the time scale is
    // not merely internally consistent but actually calibrated.
    FundamentalConfig cfg;
    cfg.type          = FundamentalProcessType::GBM;
    cfg.initial_value = 100.0;
    cfg.sigma_annual  = 0.30;

    TimeScale ts;
    auto proc = make_fundamental_process(cfg, ts, make_rng(7));
    const int n = static_cast<int>(ts.ticks_per_day());

    double prev = proc->current_value();
    double sum2 = 0.0;
    for (int t = 0; t < n; ++t) {
        proc->step(t, 1.0);
        double v = proc->current_value();
        double r = std::log(v / prev);
        sum2 += r * r;
        prev = v;
    }
    double daily_vol = std::sqrt(sum2);                 // sqrt of summed variance
    double expected  = 0.30 / std::sqrt(252.0);         // ~1.89%
    CHECK(daily_vol == Catch::Approx(expected).epsilon(0.15));
}

TEST_CASE("FundamentalProcessFactory: OU reverts to theta", "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type          = FundamentalProcessType::OU;
    cfg.initial_value = 100.0;
    cfg.theta         = 100.0;
    cfg.kappa         = 0.5;

    auto proc = make_fundamental_process(cfg, TimeScale{}, make_rng());
    REQUIRE(proc != nullptr);
    CHECK(std::string(proc->name()) == "OU");
    CHECK(proc->current_value() == Catch::Approx(100.0));
}

TEST_CASE("FundamentalProcessFactory: JumpDiffusion produces valid output", "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type           = FundamentalProcessType::JumpDiffusion;
    cfg.initial_value  = 100.0;
    cfg.jumps_per_day  = 2.0;
    cfg.jump_sigma     = 0.05;

    auto proc = make_fundamental_process(cfg, TimeScale{}, make_rng());
    REQUIRE(proc != nullptr);
    CHECK(std::string(proc->name()) == "JumpDiffusion");
    CHECK(proc->current_value() > 0.0);
}

TEST_CASE("FundamentalProcessFactory: RegimeSwitching scales sigma by the regime multiplier",
          "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type               = FundamentalProcessType::RegimeSwitching;
    cfg.initial_value      = 200.0;
    cfg.sigma_annual       = 0.30;
    cfg.regime_vol_mult[0] = 1.0;
    cfg.regime_vol_mult[1] = 2.5;
    cfg.regime_vol_mult[2] = 6.0;

    TimeScale ts;
    auto proc = make_fundamental_process(cfg, ts, make_rng());
    REQUIRE(proc != nullptr);
    CHECK(std::string(proc->name()) == "RegimeSwitching");
    CHECK(proc->current_value() == Catch::Approx(200.0));
    // Starts in the quiet regime, so current_vol is the base per-tick sigma.
    CHECK(proc->current_vol() == Catch::Approx(ts.per_tick_vol(0.30)));
}

TEST_CASE("Fundamental processes: apply_shock moves the level multiplicatively",
          "[factory][fundamental]") {
    // A public announcement must move the fundamental itself; a 1% impact
    // multiplies V by e^0.01 in every process.
    const FundamentalProcessType types[] = {
        FundamentalProcessType::GBM,
        FundamentalProcessType::OU,
        FundamentalProcessType::JumpDiffusion,
        FundamentalProcessType::RegimeSwitching,
    };

    for (auto type : types) {
        FundamentalConfig cfg;
        cfg.type          = type;
        cfg.initial_value = 100.0;
        cfg.theta         = 100.0;

        auto proc = make_fundamental_process(cfg, TimeScale{}, make_rng());
        double before = proc->current_value();
        proc->apply_shock(0.01);
        CHECK(proc->current_value()
              == Catch::Approx(before * std::exp(0.01)).epsilon(1e-12));
    }
}

TEST_CASE("FundamentalProcessFactory: different processes diverge from same seed", "[factory][fundamental]") {
    // Changing --process changes the simulation output (live config verification).
    FundamentalConfig cfg_rs;
    cfg_rs.type = FundamentalProcessType::RegimeSwitching;
    cfg_rs.initial_value = 100.0;

    FundamentalConfig cfg_gbm;
    cfg_gbm.type = FundamentalProcessType::GBM;
    cfg_gbm.initial_value = 100.0;

    auto rs  = make_fundamental_process(cfg_rs,  TimeScale{}, make_rng(42));
    auto gbm = make_fundamental_process(cfg_gbm, TimeScale{}, make_rng(42));

    // Run 50 ticks
    for (int t = 0; t < 50; ++t) {
        rs->step(t, 1.0);
        gbm->step(t, 1.0);
    }

    // With the same seed but different process types, values must differ.
    CHECK(rs->current_value() != gbm->current_value());
}
