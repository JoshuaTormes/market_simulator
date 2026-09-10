#include "FundamentalProcessFactory.h"
#include "GBMProcess.h"
#include "OUProcess.h"
#include "JumpDiffusionProcess.h"
#include "RegimeSwitchingProcess.h"

std::unique_ptr<IFundamentalValueProcess>
make_fundamental_process(const FundamentalConfig& cfg, const TimeScale& ts,
                         std::mt19937_64 rng) {
    // One volatility knob for every process: the annualized sigma, converted to
    // per-tick by square-root-of-time scaling.  A process must never carry its
    // own idea of what a tick is worth.
    const double sigma_tick = ts.per_tick_vol(cfg.sigma_annual);
    const double mu_tick    = cfg.mu_annual / ts.ticks_per_year();

    switch (cfg.type) {

    case FundamentalProcessType::GBM: {
        GBMProcess::Config c;
        c.s0    = cfg.initial_value;
        c.mu    = mu_tick;
        c.sigma = sigma_tick;
        return std::make_unique<GBMProcess>(c, std::move(rng));
    }

    case FundamentalProcessType::OU: {
        OUProcess::Config c;
        c.s0    = cfg.initial_value;
        c.theta = cfg.theta;
        c.kappa = cfg.kappa;
        // OU diffuses in level units, so the log-scale sigma is multiplied by
        // the long-run mean to give the same relative volatility.
        c.sigma = sigma_tick * cfg.theta;
        return std::make_unique<OUProcess>(c, std::move(rng));
    }

    case FundamentalProcessType::JumpDiffusion: {
        JumpDiffusionProcess::Config c;
        c.s0          = cfg.initial_value;
        c.mu          = mu_tick;
        c.sigma       = sigma_tick;
        c.lambda_jump = ts.per_tick_rate(cfg.jumps_per_day);
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
            // Regimes multiply the base volatility; the drift is the same in
            // all of them, because a crash is a volatility event and not a
            // change in expected return.
            c.regimes[i].mu    = mu_tick;
            c.regimes[i].sigma = sigma_tick * cfg.regime_vol_mult[i];
            for (int j = 0; j < 3; ++j)
                c.trans[i][j] = cfg.regime_trans[i][j];
        }
        return std::make_unique<RegimeSwitchingProcess>(c, std::move(rng));
    }

    }
}
