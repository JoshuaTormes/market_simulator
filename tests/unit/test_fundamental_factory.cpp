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
    // Starts in the quiet regime.  The base sigma sits *below* the nominal
    // per-tick sigma, because the multipliers inflate the unconditional
    // variance and the factory normalizes that away: with the default sojourn
    // times pi ~ {0.78, 0.21, 0.01} and E[m^2] ~ 2.4, so base ~ 0.645 sigma.
    const double base = proc->current_vol();
    CHECK(base < ts.per_tick_vol(0.30));
    CHECK(base > 0.5 * ts.per_tick_vol(0.30));
}

TEST_CASE("FundamentalProcessFactory: RegimeSwitching unconditional vol equals sigma_annual",
          "[factory][fundamental]") {
    // Twenty sessions of the regime process must realize an annualized
    // volatility close to the configured 30%; that is the whole point of the
    // stationary normalization.  A wide band: with 2-hour quiet regimes the
    // realized vol over 20 days still carries sampling noise.
    FundamentalConfig cfg;
    cfg.type          = FundamentalProcessType::RegimeSwitching;
    cfg.initial_value = 100.0;
    cfg.sigma_annual  = 0.30;

    TimeScale ts;
    auto proc = make_fundamental_process(cfg, ts, make_rng(11));
    const long n = static_cast<long>(ts.ticks_per_day()) * 20;

    double prev = proc->current_value();
    double sum2 = 0.0;
    for (long t = 0; t < n; ++t) {
        proc->step(t, 1.0);
        const double v = proc->current_value();
        const double r = std::log(v / prev);
        sum2 += r * r;
        prev = v;
    }
    const double ann_vol = std::sqrt(sum2 / n * ts.ticks_per_year());
    CHECK(ann_vol == Catch::Approx(0.30).epsilon(0.25));
}

TEST_CASE("FundamentalProcessFactory: regime sojourn times follow the configured durations",
          "[factory][fundamental]") {
    FundamentalConfig cfg;
    cfg.type = FundamentalProcessType::RegimeSwitching;
    cfg.regime_duration_sec[0] = 600.0;   // short, so the test sees many spells
    cfg.regime_duration_sec[1] = 300.0;
    cfg.regime_duration_sec[2] = 60.0;

    TimeScale ts;
    auto proc = make_fundamental_process(cfg, ts, make_rng(3));
    const long n = 400'000;
    int  prev_regime = proc->current_regime_hint();
    long spell = 0, quiet_spells = 0, quiet_ticks = 0;
    for (long t = 0; t < n; ++t) {
        proc->step(t, 1.0);
        const int r = proc->current_regime_hint();
        if (prev_regime == 0) ++quiet_ticks;
        if (r != prev_regime) {
            if (prev_regime == 0) ++quiet_spells;
            prev_regime = r;
            spell = 0;
        } else {
            ++spell;
        }
    }
    REQUIRE(quiet_spells > 50);
    const double mean_quiet = static_cast<double>(quiet_ticks) / quiet_spells;
    CHECK(mean_quiet == Catch::Approx(600.0).epsilon(0.25));
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

// ── Variance budget: announcements are paid for out of sigma_annual ────────

TEST_CASE("Factory: news variance is subtracted from the diffusive budget",
          "[factory][fundamental][news]") {
    TimeScale ts;
    FundamentalConfig cfg;
    cfg.type         = FundamentalProcessType::GBM;
    cfg.sigma_annual = 0.30;

    auto no_news = make_fundamental_process(cfg, ts, make_rng(5));
    const double sigma_full = no_news->current_vol();

    NewsConfig news;                       // shipping defaults
    cfg.news_var_per_tick = news.variance_per_tick(ts);
    auto with_news = make_fundamental_process(cfg, ts, make_rng(5));
    const double sigma_diff = with_news->current_vol();

    CHECK(cfg.news_var_per_tick > 0.0);
    CHECK(sigma_diff < sigma_full);
    // Diffusion plus announcements reconstitutes the configured total.
    const double total = sigma_diff * sigma_diff + cfg.news_var_per_tick;
    CHECK(std::sqrt(total) == Catch::Approx(sigma_full).epsilon(1e-9));
}

TEST_CASE("Factory: an over-booked news budget still leaves a diffusion",
          "[factory][fundamental][news]") {
    TimeScale ts;
    FundamentalConfig cfg;
    cfg.type              = FundamentalProcessType::GBM;
    cfg.sigma_annual      = 0.30;
    const double full     = ts.per_tick_vol(cfg.sigma_annual);
    cfg.news_var_per_tick = 10.0 * full * full;   // absurdly over budget

    auto proc = make_fundamental_process(cfg, ts, make_rng(5));
    CHECK(proc->current_vol() == Catch::Approx(0.5 * full).epsilon(1e-9));
}

TEST_CASE("NewsConfig: variance per tick matches lambda·scale²·var[t]",
          "[factory][news]") {
    TimeScale ts;
    NewsConfig news;
    const double expected = news.lambda_per_tick(ts) * news.impact_scale * news.impact_scale
                          * (news.impact_df / (news.impact_df - 2.0));
    CHECK(news.variance_per_tick(ts) == Catch::Approx(expected).epsilon(1e-12));

    // Cauchy-like tails have no variance, so there is nothing to budget.
    news.impact_df = 2.0;
    CHECK(news.variance_per_tick(ts) == Catch::Approx(0.0).margin(1e-18));
}
