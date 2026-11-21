#pragma once
// Hamilton (1989) Regime-Switching: Markov chain between {quiet, elevated, high_vol}.
// In each regime, the process follows GBM with regime-specific (μ, σ).
// Transition matrix P[from][to] is row-stochastic (rows sum to 1).
// Reference: Hamilton, J.D. (1989). "A new approach to the economic analysis of
//            nonstationary time series and the business cycle." Econometrica 57(2).
#include "IFundamentalValueProcess.h"
#include <array>
#include <random>

class RegimeSwitchingProcess : public IFundamentalValueProcess {
public:
    static constexpr int kRegimes = 3; // quiet=0, elevated=1, high_vol=2 (zero drift in all)

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
            {0.0,  0.80e-4},  // 0: quiet    (σ_annual 30% at 1 s, after the stationary normalization)
            {0.0,  2.00e-4},  // 1: elevated (2.5x)
            {0.0,  4.80e-4}   // 2: high vol (6x)
        }};
        // Row i = transition probabilities from regime i.  Expected duration
        // is 1/(1-p_ii): 7200 / 1800 / 180 ticks (2 h / 30 min / 3 min at 1 s
        // per tick).  The factory derives these from FundamentalConfig.
        std::array<std::array<double, kRegimes>, kRegimes> trans = {{
            {1.0 - 1.0/7200.0, 0.8/7200.0, 0.2/7200.0},
            {0.8/1800.0, 1.0 - 1.0/1800.0, 0.2/1800.0},
            {0.3/180.0,  0.7/180.0,  1.0 - 1.0/180.0}
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
