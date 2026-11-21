#include "FundamentalProcessFactory.h"
#include "GBMProcess.h"
#include "OUProcess.h"
#include "JumpDiffusionProcess.h"
#include "RegimeSwitchingProcess.h"
#include <algorithm>
#include <cmath>

std::unique_ptr<IFundamentalValueProcess>
make_fundamental_process(const FundamentalConfig& cfg, const TimeScale& ts,
                         std::mt19937_64 rng) {
    // One volatility knob for every process: the annualized sigma, converted to
    // per-tick by square-root-of-time scaling.  A process must never carry its
    // own idea of what a tick is worth.
    const double sigma_total = ts.per_tick_vol(cfg.sigma_annual);
    const double mu_tick     = cfg.mu_annual / ts.ticks_per_year();

    // Variance budget (Merton 1976): sigma_annual is the volatility of the
    // *value process*, not of one of its components.  Public announcements hit
    // the same log V through apply_shock(), so their variance is subtracted
    // here and the diffusion gets the residual.  Without this the announcement
    // stream is pure extra volatility: at 8 events/day and a 0.4% scale it
    // carried more variance than the whole 30% budget and V realized 43%.
    // The floor keeps a diffusion alive even when a caller over-books the
    // budget — the regimes are what produce volatility clustering, and a
    // market driven only by announcement jumps has none.
    const double total_var = sigma_total * sigma_total;
    const double news_var  = std::max(0.0, cfg.news_var_per_tick);
    const double sigma_tick = std::sqrt(std::max(0.25 * total_var, total_var - news_var));

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

        // Transition matrix from sojourn times: P(stay) = 1 - dt/duration, and
        // the exit mass is split by regime_exit_split.
        for (int i = 0; i < 3; ++i) {
            const double dur_ticks = std::max(1.0, cfg.regime_duration_sec[i] / ts.seconds_per_tick);
            const double p_exit    = std::min(1.0, 1.0 / dur_ticks);
            double split_sum = 0.0;
            for (int j = 0; j < 3; ++j) if (j != i) split_sum += std::max(0.0, cfg.regime_exit_split[i][j]);
            for (int j = 0; j < 3; ++j) {
                if (j == i) { c.trans[i][j] = 1.0 - p_exit; continue; }
                const double w = (split_sum > 0.0) ? std::max(0.0, cfg.regime_exit_split[i][j]) / split_sum
                                                   : 0.5;
                c.trans[i][j] = p_exit * w;
            }
        }

        // Stationary distribution by power iteration, then normalize the base
        // sigma so that sum_i pi_i (m_i sigma_base)^2 = sigma_tick^2.
        double pi[3] = {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0};
        for (int it = 0; it < 20000; ++it) {
            double nx[3] = {0.0, 0.0, 0.0};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) nx[j] += pi[i] * c.trans[i][j];
            double diff = 0.0;
            for (int j = 0; j < 3; ++j) { diff += std::abs(nx[j] - pi[j]); pi[j] = nx[j]; }
            if (diff < 1e-14) break;
        }
        double m2 = 0.0;
        for (int i = 0; i < 3; ++i) m2 += pi[i] * cfg.regime_vol_mult[i] * cfg.regime_vol_mult[i];
        const double sigma_base = (m2 > 0.0) ? sigma_tick / std::sqrt(m2) : sigma_tick;

        for (int i = 0; i < 3; ++i) {
            // Regimes multiply the base volatility; the drift is the same in
            // all of them, because the high-vol state is a volatility event
            // and not a change in expected return.
            c.regimes[i].mu    = mu_tick;
            c.regimes[i].sigma = sigma_base * cfg.regime_vol_mult[i];
        }
        return std::make_unique<RegimeSwitchingProcess>(c, std::move(rng));
    }

    }
}
