#pragma once
// Poisson news arrivals with fat-tailed (Student-t, ν=3) magnitude.
// Number of events per tick ~ Poisson(λ).
// Impact magnitude ~ scale · Student-t(ν), sign randomized uniformly.
// Reference: Cont, R. (2001) "Empirical properties of asset returns: stylized facts and
//            statistical issues." Quantitative Finance 1(2).
#include "INewsEventProcess.h"
#include "core/EventBus.h"
#include <random>
#include <string>

class PoissonNewsProcess : public INewsEventProcess {
public:
    struct Config {
        double      lambda            = 0.01;  // avg news events per tick
        double      magnitude_scale   = 0.005; // |impact| scale in log-return units
        int         student_t_df      = 3;     // Student-t degrees of freedom
        double      duration_ticks    = 20.0;
        double      dispersion_sigma  = 0.3;   // agent heterogeneity in impact interpretation
        std::string ticker            = "MAIN";
    };

    // bus may be nullptr — events returned via step() only, not published
    PoissonNewsProcess(Config cfg, std::mt19937_64 rng, EventBus* bus = nullptr);

    std::vector<NewsEvent> step(Tick now) override;
    const char* name() const override { return "PoissonNews"; }

private:
    Config cfg_;
    EventBus* bus_;
    std::mt19937_64 rng_;
    std::poisson_distribution<int>    poisson_;
    std::normal_distribution<double>  norm_{0.0, 1.0};
    std::chi_squared_distribution<double> chi2_;
    std::bernoulli_distribution side_{0.5};

    double sample_student_t();
};
