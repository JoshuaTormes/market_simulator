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
    // 200 lots per side: quoted depth has to exceed a typical aggressive order
    // (institutional children cap at 200) or one market order empties the book
    // and the mid stops existing until the next requote.
    int mm_qty = 200;
    // Quote geometry in price ticks — the book is integer, so the maker is too.
    double half_spread_min_ticks = 1.0;
    double vol_mult              = 1.0;
    double inventory_skew_ticks  = 4.0;
    double adverse_sel_ticks_per_lot = 0.01;
    // 600 lots = three full-size quotes: the maker must be able to absorb a few
    // complete fills before it withdraws a side, or the book goes one-sided by
    // construction rather than because liquidity ran out.
    double q_soft                = 600.0;
    // Belief dynamics.
    double lambda_kyle  = 0.06;
    double belief_decay = 0.02;
};

struct NoiseTraderParams {
    int count = 25;
    ParamRange<double> p_act_range    = {0.02, 0.10};
    // With size_sigma = 1.5 the lognormal mean is exp(mu + sigma^2/2), so this
    // range puts E[size] between 8 and 23 lots, ~15 on average.
    ParamRange<double> size_mu_range  = {1.0,  2.0};
    double size_sigma                 = 1.5;
    // Inventory scale of the side tilt.  Uninformed does not mean unbounded:
    // without this the cohort random-walks into its position limit and its
    // rejected flow becomes a spurious directional push.
    double q_scale                    = 500.0;
    int    max_position               = 5000;
};

struct InformedTraderParams {
    int    count             = 3;
    double signal_noise_log  = 5e-4;  // multiplicative error on the observed fundamental
    double lambda_inv        = 0.5;   // lots per tick of pricing gap
    double margin_ticks      = 1.0;   // gap must beat half-spread by this much
    int    max_order_size    = 200;
    int    unwind_qty        = 50;    // lots returned per tick once the gap is closed
    int    max_position      = 4000;
    double pareto_alpha      = 1.5;   // Pareto tail exponent for order sizing
    double q_soft_frac       = 0.6;   // own risk budget as a fraction of max_position
};

struct MomentumParams {
    // count=3: three trend followers against two mean reverters.  The pair of
    // opposing strategies is what keeps the return ACF near zero while still
    // producing volatility clustering; a single momentum trader with no
    // counterparty just adds one-sided herding.
    int count = 3;
    ParamRange<int>    fast_range     = {5,  15};
    ParamRange<int>    slow_range     = {20, 50};
};

struct MeanReverterParams {
    // count=2: the counterweight to the momentum cohort (see MomentumParams).
    int count = 2;
    ParamRange<double> entry_z_range  = {1.5, 3.0};
    ParamRange<int>    window_range   = {20,  60};
};

struct ValueInvestorParams {
    int count = 0;
    ParamRange<double> k_range        = {0.01, 0.05};
};

struct InstitutionalParams {
    int    count          = 2;      // two desks: order splitting drives fat tails
    // Parent arrival process — a desk that receives one order per run stops
    // contributing flow after the first hundred ticks.
    double arrival_lambda = 0.002;  // P(new parent | idle tick) ≈ one per 500 ticks
    int    parent_min     = 200;    // Pareto parent size, truncated to
    int    parent_max     = 3000;   // [parent_min, parent_max]
    double parent_alpha   = 1.5;    // Pareto tail exponent for parent size
    double q_scale        = 1500.0; // inventory scale of the parent-side tilt
    int    slices         = 20;
    int    ticks_between  = 5;
    double pareto_alpha   = 1.5;    // Pareto tail exponent for child order sizing
    int    max_child_qty  = 200;    // hard cap per child order
    int    max_position   = 4000;
};

struct StopLossParams {
    int   count                       = 6;
    int64_t entry_price_ticks         = 10000; // reference price for seeded positions
    // 1.5-4%: with the price now tracking the fundamental to within ~0.2% the
    // old 4-8% band was almost never reached, so no cascade ever formed.
    ParamRange<double> trigger_range  = {0.015, 0.04};
    ParamRange<int>    qty_range      = {10,   75};  // 6×75=450 < 600 MM depth: avoids bid depletion
    // Ticks flat after a stop fires before a new position is established.
    ParamRange<int>    cooldown_range = {200, 800};
};

struct NewsReactorParams {
    // lag_range = [0,1]: short lags, so the cohort starts reacting within a tick
    // of the announcement.  Each reactor then fires exactly once, at a moment
    // drawn inside the event window — the heterogeneity across the 8 reactors
    // is what spreads the impact over several ticks, not a repeated order from
    // each of them.
    int count = 8;
    int base_qty = 50;                             // lots per reaction tick for a typical headline
    ParamRange<double> lag_range      = {0, 1};
    ParamRange<double> sensitivity_range = {0.5, 2.0};
    // Reference impact base_qty is quoted against — keep in sync with
    // NewsConfig::impact_scale, or reactions collapse to the 1-lot floor.
    double impact_scale = 0.004;
    int    max_position = 2000;
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
