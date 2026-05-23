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
        // Degrees of freedom for Student-t innovations. ν=3.5 → tail index α≈3.5 ∈ [1.8,6].
        // Innovations are normalized to unit variance so calibrated sigma values are preserved.
        double nu = 3.5;
        // mu_low/high = +7.3e-5 compensates crash drift so E[delta_log_s]=0 long-run.
        // Without compensation: crash (π≈0.6%, μ=-0.010) causes -3.0 cumulative log-drift
        // over 50k ticks, collapsing the fundamental near zero and distorting all stylized facts.
        // Compensating drift: π_low*μ_low + π_high*μ_high + π_crash*μ_crash ≈ 0.
        std::array<Regime, kRegimes> regimes = {{
            {+7.3e-5,  0.003},  // 0: low_vol  (quiet random walk, tiny upward drift to stay trend-neutral)
            {+7.3e-5,  0.010},  // 1: high_vol (elevated vol, same compensating drift)
            {-0.010,   0.025}   // 2: crash    (brief intense vol, strong negative drift → negative skew)
        }};
        // Row i = transition probabilities from regime i.
        // Steady-state: ~86% low_vol, ~13% high_vol, ~0.6% crash.
        // Crashes are brief (avg ~1.5 ticks) but frequent enough for fat tails and negative skew.
        std::array<std::array<double, kRegimes>, kRegimes> trans = {{
            {0.990, 0.007, 0.003},  // low_vol  → mostly stays, some to high_vol/crash
            {0.050, 0.940, 0.010},  // high_vol → recovers 5%/tick, some to crash
            {0.350, 0.300, 0.350}   // crash    → fast recovery (avg ~1.5 ticks)
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
    std::student_t_distribution<double> student_t_{3.5};
    std::uniform_real_distribution<double> uni_{0.0, 1.0};

    void transition_regime();
};
