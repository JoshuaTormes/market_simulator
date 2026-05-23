#include "analysis/FlashCrashDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>

std::vector<FlashCrashEvent> FlashCrashDetector::detect(
    const std::vector<double>&   prices,
    const std::vector<uint64_t>& ticks,
    const Config& cfg)
{
    std::vector<FlashCrashEvent> events;
    int n = static_cast<int>(prices.size());
    if (n < cfg.sigma_window + cfg.max_crash_ticks + cfg.max_recovery_ticks) return events;

    // Build log-return series.
    std::vector<double> rets(n, 0.0);
    for (int i = 1; i < n; ++i) {
        if (prices[i - 1] > 0.0 && prices[i] > 0.0)
            rets[i] = std::log(prices[i] / prices[i - 1]);
    }

    int last_crash_end = 0;  // prevent overlapping events

    for (int i = cfg.sigma_window; i < n - cfg.max_crash_ticks - cfg.max_recovery_ticks; ++i) {
        if (i <= last_crash_end) continue;

        // Rolling σ from the preceding window.
        double mean_r = 0.0;
        for (int j = i - cfg.sigma_window; j < i; ++j) mean_r += rets[j];
        mean_r /= cfg.sigma_window;

        double var_r = 0.0;
        for (int j = i - cfg.sigma_window; j < i; ++j)
            var_r += (rets[j] - mean_r) * (rets[j] - mean_r);
        double sigma_r = std::sqrt(var_r / cfg.sigma_window);
        if (sigma_r < 1e-12) continue;

        double peak_price = prices[i];

        // Search for trough within max_crash_ticks.
        int    trough_idx = i;
        double trough_price = peak_price;
        for (int j = i + 1; j <= i + cfg.max_crash_ticks && j < n; ++j) {
            if (prices[j] < trough_price) {
                trough_price = prices[j];
                trough_idx   = j;
            }
        }

        if (trough_idx == i) continue;

        double drawdown_pct    = (peak_price - trough_price) / peak_price;
        double cumul_log_ret   = std::log(trough_price / peak_price);
        double drawdown_sigmas = std::abs(cumul_log_ret) / sigma_r;

        if (drawdown_sigmas < cfg.sigma_threshold) continue;

        // Check recovery within max_recovery_ticks after trough.
        bool     recovered    = false;
        uint64_t recovery_tick = 0;
        double   recovery_target = trough_price + cfg.recovery_fraction * (peak_price - trough_price);

        for (int j = trough_idx + 1;
             j <= trough_idx + cfg.max_recovery_ticks && j < n; ++j) {
            if (prices[j] >= recovery_target) {
                recovered     = true;
                recovery_tick = ticks[j];
                break;
            }
        }

        FlashCrashEvent ev;
        ev.start_tick      = ticks[i];
        ev.trough_tick     = ticks[trough_idx];
        ev.drawdown_pct    = drawdown_pct;
        ev.drawdown_sigmas = drawdown_sigmas;
        ev.recovered       = recovered;
        ev.recovery_tick   = recovery_tick;
        events.push_back(ev);

        last_crash_end = trough_idx + cfg.max_recovery_ticks;
    }

    return events;
}
