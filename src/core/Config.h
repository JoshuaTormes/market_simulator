#pragma once
#include "Types.h"
#include "Clock.h"
#include <string>
#include <cstdint>

// ── Per-agent-type population ranges ──────────────────────────────────────

template<typename T>
struct ParamRange {
    T min_val;
    T max_val;
};

struct MarketMakerParams {
    int count = 3;
    int mm_qty = 100;                              // shares per quote side
    ParamRange<double> gamma_range    = {0.05, 0.20};
    ParamRange<double> k_range        = {1.0,  4.0};
    ParamRange<double> sigma_range    = {0.01, 0.04};  // fractional vol per tick
    double T_horizon                  = 100.0;  // ticks
    double beta_ofi                   = 0.35;   // Bayesian belief update speed from OFI
    double adverse_sel                = 0.04;   // adverse selection spread multiplier
    double belief_decay               = 0.05;   // reversion speed toward perceived mid
};

struct NoiseTraderParams {
    int count = 25;
    ParamRange<double> p_act_range    = {0.02, 0.10};
    ParamRange<double> size_mu_range  = {0.5,  2.5};
    double size_sigma                 = 1.5;
};

struct InformedTraderParams {
    int count = 2;
    double lambda_inv                 = 0.5;
    double pareto_alpha               = 1.5;  // Pareto tail exponent for order sizing
    int    max_order_size             = 100;  // raised from 50 to allow crash differentiation
    ParamRange<double> signal_lag_range = {0, 3};
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
    // GBM / per-regime shared drift+vol
    double mu                   = 0.0;
    double sigma                = 0.01;
    // OU
    double kappa                = 0.05;
    double theta                = 100.0;
    // Jump-diffusion
    double jump_intensity       = 0.005;
    double jump_mean            = 0.0;
    double jump_sigma           = 0.03;
    // Regime switching — defaults match RegimeSwitchingProcess internal calibration
    double nu                   = 3.5;    // Student-t degrees of freedom for innovations
    bool   gaussian_innovations = false;  // if true, use N(0,1) instead of Student-t
    double regime_sigma[3]      = {0.003, 0.010, 0.035};  // low_vol, high_vol, crash
    double regime_mu[3]         = {0.0,   0.0,   0.0};    // pure martingale in all regimes
    // Transition matrix (row = from, col = to)
    // high_vol: avg 33 ticks (1/0.030); crash: avg 6.7 ticks (1/0.150)
    double regime_trans[3][3]   = {
        {0.990, 0.007, 0.003},
        {0.020, 0.970, 0.010},
        {0.050, 0.100, 0.850}
    };
};

// ── News process config ────────────────────────────────────────────────────

struct NewsConfig {
    double lambda               = 0.002;  // events per tick (reduced to limit return ACF)
    double impact_df            = 3.0;    // Student-t degrees of freedom for magnitude
    double impact_scale         = 0.02;   // scale of the t-distribution
    int    duration_ticks       = 5;      // ticks of sustained reactor firing per event
    double dispersion           = 0.5;    // cross-agent heterogeneity
};

// ── Root simulation config ─────────────────────────────────────────────────

struct SimulationConfig {
    uint64_t  seed              = 42;
    uint64_t  max_ticks         = 100'000;
    ClockMode clock_mode        = ClockMode::AsFastAsPossible;
    double    seconds_per_tick  = 0.001;
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
