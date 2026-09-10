#pragma once
#include "Types.h"
#include "Clock.h"
#include <cmath>
#include <string>
#include <cstdint>

// ── Time scale ─────────────────────────────────────────────────────────────
// A tick is a fixed slice of trading time.  Every quantity with a time
// dimension — volatility, news arrival rate, agent horizons — is derived from
// these three numbers, so the model has one coherent clock instead of a
// per-component guess.  Changing seconds_per_tick rescales the whole model
// consistently.
struct TimeScale {
    double seconds_per_tick = 1.0;       // 1 tick = 1 second of trading
    double seconds_per_day  = 23400.0;   // 6.5 h US equity session
    double trading_days     = 252.0;

    double ticks_per_day()  const { return seconds_per_day / seconds_per_tick; }
    double ticks_per_year() const { return ticks_per_day() * trading_days; }

    // Per-tick volatility implied by an annualized volatility, under
    // square-root-of-time scaling.  σ_annual = 30% → σ_tick ≈ 1.24e-4.
    double per_tick_vol(double sigma_annual) const {
        return sigma_annual / std::sqrt(ticks_per_year());
    }

    // Convert a per-day rate (e.g. news announcements per session) to per tick.
    double per_tick_rate(double per_day) const { return per_day / ticks_per_day(); }
};

// ── Per-agent-type population ranges ──────────────────────────────────────

template<typename T>
struct ParamRange {
    T min_val;
    T max_val;
};

struct MarketMakerParams {
    int count  = 3;
    int mm_qty = 100;                       // lots per quote side at zero inventory
    // Quote geometry in price ticks — the book is integer, so the maker is too.
    double half_spread_min_ticks = 1.0;
    double vol_mult              = 1.0;
    double inventory_skew_ticks  = 4.0;
    double adverse_sel_ticks_per_lot = 0.01;
    double q_soft                = 300.0;   // position where the growing side stops quoting
    // Belief dynamics.
    double lambda_kyle  = 0.06;
    double belief_decay = 0.02;
};

struct NoiseTraderParams {
    int count = 25;
    ParamRange<double> p_act_range    = {0.02, 0.10};
    ParamRange<double> size_mu_range  = {0.5,  2.5};
    double size_sigma                 = 1.5;
};

struct InformedTraderParams {
    int    count             = 3;
    double signal_noise_log  = 5e-4;  // multiplicative error on the observed fundamental
    double lambda_inv        = 0.5;   // lots per tick of pricing gap
    double margin_ticks      = 1.0;   // gap must beat half-spread by this much
    int    max_order_size    = 200;
    int    unwind_qty        = 50;    // lots returned per tick once the gap is closed
    int    max_position      = 2000;
    double pareto_alpha      = 1.5;   // Pareto tail exponent for order sizing
};

struct MomentumParams {
    // count=1: calibrated dose — one momentum trader provides mild herding without
    // dominating the ACF. Full calibration is done in Etapa 8.
    int count = 1;
    ParamRange<int>    fast_range     = {5,  15};
    ParamRange<int>    slow_range     = {20, 50};
};

struct MeanReverterParams {
    int count = 0;
    ParamRange<double> entry_z_range  = {1.5, 3.0};
    ParamRange<int>    window_range   = {20,  60};
};

struct ValueInvestorParams {
    int count = 0;
    ParamRange<double> k_range        = {0.01, 0.05};
};

struct InstitutionalParams {
    int    count         = 1;     // re-enabled: Pareto sizing drives fat tails
    int    parent_qty    = 500;
    int    slices        = 20;
    double pareto_alpha  = 1.5;   // Pareto tail exponent for child order sizing
    int    max_child_qty = 200;   // hard cap per child order
};

struct StopLossParams {
    int   count                       = 6;
    int64_t entry_price_ticks         = 10000; // reference price for seeded positions
    ParamRange<double> trigger_range  = {0.04, 0.08};  // 4-8% drop: avoids noise false-triggers
    ParamRange<int>    qty_range      = {10,   75};  // 6×75=450 < 600 MM depth: avoids bid depletion
};

struct NewsReactorParams {
    // lag_range = [0,1]: short lags so reactors fire within 1 tick of news.
    // Each reactor fires once per tick for the full event duration (duration_ticks),
    // creating sustained directional OFI that drives vol clustering (Facts 3 & 4).
    int count = 8;
    int base_qty = 50;                             // shares per reaction tick (8×50=400 < 600 MM depth)
    ParamRange<double> lag_range      = {0, 1};
    ParamRange<double> sensitivity_range = {0.5, 2.0};
};

// ── Population config ──────────────────────────────────────────────────────

struct PopulationConfig {
    MarketMakerParams   market_makers;
    NoiseTraderParams   noise_traders;
    InformedTraderParams informed_traders;
    MomentumParams      momentum_traders;
    MeanReverterParams  mean_reverters;
    ValueInvestorParams value_investors;
    InstitutionalParams institutionals;
    StopLossParams      stop_loss;
    NewsReactorParams   news_reactors;
};

// ── Fundamental value process config ──────────────────────────────────────

enum class FundamentalProcessType { GBM, OU, JumpDiffusion, RegimeSwitching };

struct FundamentalConfig {
    FundamentalProcessType type = FundamentalProcessType::RegimeSwitching;
    double initial_value        = 100.0;

    // Annualized volatility of the fundamental — the one volatility knob.
    // 30% is the typical range for a large-cap equity.  The per-tick sigma
    // used by every process is derived from this via TimeScale, so the value
    // no longer depends on what a "tick" happens to mean.
    double sigma_annual        = 0.30;
    // Drift of the log fundamental, per year.  Zero: a martingale.  Any
    // non-zero value here is a claim about expected returns, not a fix for a
    // process that collapses.
    double mu_annual           = 0.0;

    // OU only: mean-reversion speed per tick, and long-run mean.
    double kappa                = 0.05;
    double theta                = 100.0;
    // Jump-diffusion only: jumps per day and log-jump size.
    double jumps_per_day        = 2.0;
    double jump_mean            = 0.0;
    double jump_sigma           = 0.03;

    // Regime switching.
    double nu                   = 3.5;    // Student-t degrees of freedom for innovations
    bool   gaussian_innovations = false;  // if true, use N(0,1) instead of Student-t
    // Regimes are multipliers on the base per-tick sigma, not absolute levels:
    // quiet / elevated / crash.  A crash is 6x normal volatility, not a
    // different asset.
    double regime_vol_mult[3]   = {1.0, 2.5, 6.0};
    // Transition matrix (row = from, col = to).  Expected duration is
    // 1/(1-p_ii) ticks: 100 / 33 / 6.7 at these values.
    double regime_trans[3][3]   = {
        {0.990, 0.007, 0.003},
        {0.020, 0.970, 0.010},
        {0.050, 0.100, 0.850}
    };
};

// ── News process config ────────────────────────────────────────────────────

struct NewsConfig {
    // Public announcements per trading session.  8/day is roughly what a
    // liquid single name sees (earnings, guidance, sector and macro prints).
    double events_per_day       = 8.0;
    double impact_df            = 3.0;    // Student-t degrees of freedom for magnitude
    // Scale of the announcement's log-impact on the fundamental.  0.4% with
    // t(3) tails puts a typical headline at a few tens of basis points and a
    // rare one at a few percent — the size of a real earnings surprise.
    double impact_scale         = 0.004;
    int    duration_ticks       = 5;      // ticks of sustained reactor firing per event
    double dispersion           = 0.5;    // cross-agent heterogeneity

    double lambda_per_tick(const TimeScale& ts) const {
        return ts.per_tick_rate(events_per_day);
    }
};

// ── Root simulation config ─────────────────────────────────────────────────

struct SimulationConfig {
    uint64_t  seed              = 42;
    uint64_t  max_ticks         = 100'000;
    ClockMode clock_mode        = ClockMode::AsFastAsPossible;
    TimeScale time;                          // 1 tick = 1 s of trading by default
    double    accel_factor      = 1.0;
    std::string ticker          = "AAPL";
    double    tick_size         = 0.01;
    int64_t   lot_size          = 1;
    int64_t   initial_price_ticks = 10'000;  // $100.00 at tick_size=0.01
    int64_t   total_outstanding = 0;         // 0 = auto from population
    int       publish_interval_ticks = 1;
    PopulationConfig population;
    FundamentalConfig fundamental;
    NewsConfig news;
    std::string log_output_path = "sim.bin.log";
};
