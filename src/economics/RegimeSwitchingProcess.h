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
        // All μ = 0: the log-value is a martingale.  Stylized facts must emerge
        // from market microstructure, not from a drift term.
        // These sigmas are placeholders for direct unit-test construction; the
        // real values come from FundamentalConfig via the factory, which
        // derives one per-tick sigma from sigma_annual and scales it by the
        // per-regime multiplier.  Keep the two in sync only through the factory.
        std::array<Regime, kRegimes> regimes = {{
            {0.0,  1.24e-4},  // 0: quiet    (σ_annual 30% at 1 tick = 1 s)
            {0.0,  3.10e-4},  // 1: elevated (2.5x)
            {0.0,  7.44e-4}   // 2: crash    (6x)
        }};
        // Row i = transition probabilities from regime i.  Expected duration
        // is 1/(1-p_ii): 100 / 33 / 6.7 ticks.
        std::array<std::array<double, kRegimes>, kRegimes> trans = {{
            {0.990, 0.007, 0.003},
            {0.020, 0.970, 0.010},
            {0.050, 0.100, 0.850}
        }};
        int  initial_regime       = 0;
        // Emergence test flag: Gaussian innovations prove fat tails come from microstructure.
        bool gaussian_innovations = false;
    };

    RegimeSwitchingProcess(Config cfg, std::mt19937_64 rng);

    void   step(Tick now, double dt) override;
    void   apply_shock(double log_return) override { log_s_ += log_return; }
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
    std::student_t_distribution<double>    student_t_{3.5};
    std::normal_distribution<double>       normal_{0.0, 1.0};
    std::uniform_real_distribution<double> uni_{0.0, 1.0};

    void transition_regime();
};
