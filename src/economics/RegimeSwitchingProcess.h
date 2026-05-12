#pragma once
// Hamilton (1989) Regime-Switching: Markov chain between {low_vol, high_vol, crash}.
// In each regime, the process follows GBM with regime-specific (μ, σ).
// Transition matrix P[from][to] is row-stochastic (rows sum to 1).
// Reference: Hamilton, J.D. (1989). "A new approach to the economic analysis of
//            nonstationary time series and the business cycle." Econometrica 57(2).
#include "IFundamentalValueProcess.h"
#include <array>
#include <random>

class RegimeSwitchingProcess : public IFundamentalValueProcess {
public:
    static constexpr int kRegimes = 3; // low_vol=0, high_vol=1, crash=2

    struct Regime {
        double mu;
        double sigma;
    };

    struct Config {
        double s0 = 100.0;
        std::array<Regime, kRegimes> regimes = {{
            { 0.0002,  0.005},  // 0: low_vol  (quiet trending)
            {-0.0001,  0.02 },  // 1: high_vol (volatile)
            {-0.005,   0.05 }   // 2: crash    (sharp drawdown)
        }};
        // Row i = transition probabilities from regime i
        std::array<std::array<double, kRegimes>, kRegimes> trans = {{
            {0.97, 0.02, 0.01},
            {0.05, 0.90, 0.05},
            {0.10, 0.20, 0.70}
        }};
        int initial_regime = 0;
    };

    RegimeSwitchingProcess(Config cfg, std::mt19937_64 rng);

    void   step(Tick now, double dt) override;
    double current_value() const override;
    double current_drift() const override { return cfg_.regimes[regime_].mu; }
    double current_vol()   const override { return cfg_.regimes[regime_].sigma; }
    const char* name()     const override { return "RegimeSwitching"; }
    int    current_regime() const              { return regime_; }
    int    current_regime_hint() const override { return regime_; }

private:
    Config cfg_;
    double log_s_;
    int    regime_;
    std::mt19937_64 rng_;
    std::normal_distribution<double>  norm_{0.0, 1.0};
    std::uniform_real_distribution<double> uni_{0.0, 1.0};

    void transition_regime();
};
