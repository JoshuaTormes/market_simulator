#pragma once
// Merton (1976) Jump-Diffusion: GBM + compound Poisson jumps with log-normal jump sizes.
// dS/S = (μ − λ_j m_j) dt + σ dW + J dN
// J ~ LN(μ_j, σ_j²), N ~ Poisson(λ_j), m_j = E[J−1] = exp(μ_j + σ_j²/2) − 1
// Reference: Merton, R.C. (1976). "Option pricing when underlying stock returns are discontinuous."
#include "IFundamentalValueProcess.h"
#include <random>

class JumpDiffusionProcess : public IFundamentalValueProcess {
public:
    struct Config {
        double s0          = 100.0;
        double mu          = 0.0;
        double sigma       = 0.02;
        double lambda_jump = 0.05;  // avg jumps per dt unit
        double mu_jump     = 0.0;   // mean log-jump
        double sigma_jump  = 0.05;  // std of log-jump
    };

    JumpDiffusionProcess(Config cfg, std::mt19937_64 rng);

    void   step(Tick now, double dt) override;
    double current_value() const override;
    double current_drift() const override { return mu_eff_; }
    double current_vol()   const override { return sigma_; }
    const char* name()     const override { return "JumpDiffusion"; }

private:
    double log_s_;
    double mu_, sigma_;
    double lambda_j_, mu_j_, sigma_j_;
    double mu_eff_;   // compensated drift: μ − λ_j · (exp(μ_j + σ_j²/2) − 1)
    std::mt19937_64 rng_;
    std::normal_distribution<double>    norm_{0.0, 1.0};
    std::poisson_distribution<int>      poisson_;
};
