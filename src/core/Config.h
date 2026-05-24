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
    ParamRange<double> gamma_range    = {0.05, 0.20};
    ParamRange<double> k_range        = {1.0,  4.0};
    ParamRange<double> sigma_range    = {0.01, 0.04};  // fractional vol per tick
    double T_horizon                  = 100.0;  // ticks
};

struct NoiseTraderParams {
    int count = 25;
    ParamRange<double> p_act_range    = {0.02, 0.10};
    ParamRange<double> size_mu_range  = {0.5,  2.5};
    double size_sigma                 = 1.5;
};

struct InformedTraderParams {
    int count = 1;
    double lambda_inv                 = 0.5;
    double pareto_alpha               = 1.5;  // Pareto tail exponent for order sizing
    ParamRange<double> signal_lag_range = {0, 5};
};

struct MomentumParams {
    // count=1: calibrated dose — one momentum trader provides mild herding without
    // dominating the ACF. Full calibration is done in Etapa 8.
    int count = 1;
    ParamRange<int>    fast_range     = {5,  15};
    ParamRange<int>    slow_range     = {20, 50};
};

struct MeanReverterParams {
    int count = 8;
    ParamRange<double> entry_z_range  = {1.5, 3.0};
    ParamRange<int>    window_range   = {20,  60};
};

struct ValueInvestorParams {
    int count = 2;
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
    ParamRange<double> trigger_range  = {0.02, 0.06};
    ParamRange<int>    qty_range      = {10,  100};
};

struct NewsReactorParams {
    // lag_range = [0,1]: short lags so reactors fire within 1 tick of news.
    // Larger lags create multi-tick directional order flow → positive return ACF (Fact 2 failure).
    int count = 6;
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
    // GBM / per-regime
    double mu                   = 0.0;
    double sigma                = 0.01;
    // OU
    double kappa                = 0.05;
    double theta                = 100.0;
    // Jump-diffusion
    double jump_intensity       = 0.005;
    double jump_mean            = 0.0;
    double jump_sigma           = 0.03;
    // Regime switching (3 regimes: low_vol, high_vol, crash)
    double regime_sigma[3]      = {0.003, 0.010, 0.030};
    double regime_mu[3]         = {0.0,   0.0,  -0.010};
    // Transition matrix (row = from, col = to)
    double regime_trans[3][3]   = {
        {0.997, 0.002, 0.001},
        {0.010, 0.988, 0.002},
        {0.100, 0.150, 0.750}
    };
};

// ── News process config ────────────────────────────────────────────────────

struct NewsConfig {
    double lambda               = 0.005;  // events per tick
    double impact_df            = 3.0;    // Student-t degrees of freedom for magnitude
    double impact_scale         = 0.02;   // scale of the t-distribution
    int    duration_ticks       = 5;
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
