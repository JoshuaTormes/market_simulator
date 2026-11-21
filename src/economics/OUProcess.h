#pragma once
// Ornstein-Uhlenbeck mean-reverting process: dS = κ(θ − S) dt + σ dW
// Exact (non-Euler) discretization:
//   S_{t+dt} = θ + (S_t − θ) · exp(−κ dt) + σ · sqrt((1 − exp(−2κ dt)) / (2κ)) · Z
// Reference: Uhlenbeck & Ornstein (1930); Vasicek (1977) for applications.
#include "IFundamentalValueProcess.h"
#include <cmath>
#include <random>

class OUProcess : public IFundamentalValueProcess {
public:
    struct Config {
        double s0    = 100.0; // initial value
        double theta = 100.0; // long-run mean
        double kappa = 0.1;   // mean-reversion speed (per dt unit)
        double sigma = 1.0;   // diffusion coefficient
    };

    OUProcess(Config cfg, std::mt19937_64 rng);

    void   step(Tick now, double dt) override;
    // Level process: a log-return shock scales the level multiplicatively,
    // so the shock has the same meaning as in the log processes.
    void   apply_shock(double log_return) override { s_ *= std::exp(log_return); }
    double current_value() const override { return s_; }
    double current_drift() const override { return kappa_ * (theta_ - s_); }
    double current_vol()   const override { return sigma_; }
    const char* name()     const override { return "OU"; }

    double long_run_mean() const { return theta_; }
    double long_run_var()  const { return (sigma_ * sigma_) / (2.0 * kappa_); }

private:
    double s_;
    double theta_, kappa_, sigma_;
    std::mt19937_64 rng_;
    std::normal_distribution<double> norm_{0.0, 1.0};
};
