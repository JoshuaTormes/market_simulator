#include "FundamentalProcessFactory.h"
#include "GBMProcess.h"
#include "OUProcess.h"
#include "JumpDiffusionProcess.h"
#include "RegimeSwitchingProcess.h"

std::unique_ptr<IFundamentalValueProcess>
make_fundamental_process(const FundamentalConfig& cfg, std::mt19937_64 rng) {
    switch (cfg.type) {

    case FundamentalProcessType::GBM: {
        GBMProcess::Config c;
        c.s0    = cfg.initial_value;
        c.mu    = cfg.mu;
        c.sigma = cfg.sigma;
        return std::make_unique<GBMProcess>(c, std::move(rng));
    }

    case FundamentalProcessType::OU: {
        OUProcess::Config c;
        c.s0    = cfg.initial_value;
        c.theta = cfg.theta;
        c.kappa = cfg.kappa;
        c.sigma = cfg.sigma;
        return std::make_unique<OUProcess>(c, std::move(rng));
    }

    case FundamentalProcessType::JumpDiffusion: {
        JumpDiffusionProcess::Config c;
        c.s0          = cfg.initial_value;
        c.mu          = cfg.mu;
        c.sigma       = cfg.sigma;
        c.lambda_jump = cfg.jump_intensity;
        c.mu_jump     = cfg.jump_mean;
        c.sigma_jump  = cfg.jump_sigma;
        return std::make_unique<JumpDiffusionProcess>(c, std::move(rng));
    }

    case FundamentalProcessType::RegimeSwitching:
    default: {
        RegimeSwitchingProcess::Config c;
        c.s0                  = cfg.initial_value;
        c.nu                  = cfg.nu;
        c.gaussian_innovations = cfg.gaussian_innovations;
        for (int i = 0; i < 3; ++i) {
            c.regimes[i].mu    = cfg.regime_mu[i];
            c.regimes[i].sigma = cfg.regime_sigma[i];
            for (int j = 0; j < 3; ++j)
                c.trans[i][j] = cfg.regime_trans[i][j];
        }
        return std::make_unique<RegimeSwitchingProcess>(c, std::move(rng));
    }

    }
}
