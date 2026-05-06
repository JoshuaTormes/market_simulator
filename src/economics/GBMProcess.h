#pragma once
// Geometric Brownian Motion: dS/S = μ dt + σ dW
// Discretization: log S_{t+dt} = log S_t + (μ − σ²/2) dt + σ √dt Z, Z~N(0,1)
// Reference: Black & Scholes (1973), Merton (1973)
#include "IFundamentalValueProcess.h"
#include <random>

class GBMProcess : public IFundamentalValueProcess {
public:
    struct Config {
        double s0    = 100.0; // initial value
        double mu    = 0.0;   // drift per dt unit
        double sigma = 0.02;  // volatility per sqrt(dt) unit
    };

    GBMProcess(Config cfg, std::mt19937_64 rng);

    void   step(Tick now, double dt) override;
    double current_value() const override;
    double current_drift() const override { return mu_; }
    double current_vol()   const override { return sigma_; }
    const char* name()     const override { return "GBM"; }

private:
    double log_s_;
    double mu_, sigma_;
    std::mt19937_64 rng_;
    std::normal_distribution<double> norm_{0.0, 1.0};
};
