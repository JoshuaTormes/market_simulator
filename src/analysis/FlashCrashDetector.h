#pragma once
// Flash-crash detector: identifies rapid large drawdowns followed by partial recovery.
// A flash crash is defined as:
//   - Peak-to-trough drop > sigma_threshold * rolling_σ within max_crash_ticks
//   - At least recovery_fraction of the drop recovered within max_recovery_ticks
#include <vector>
#include <cstdint>

struct FlashCrashEvent {
    uint64_t start_tick;
    uint64_t trough_tick;
    double   drawdown_pct;     // magnitude of drop as a fraction of peak price
    double   drawdown_sigmas;  // drop in units of rolling return σ
    bool     recovered;
    uint64_t recovery_tick;    // 0 if not recovered
};

class FlashCrashDetector {
public:
    struct Config {
        double sigma_threshold    = 5.0;   // σ units required to trigger
        int    max_crash_ticks    = 10;    // peak-to-trough window
        int    max_recovery_ticks = 30;    // trough-to-recovery window
        double recovery_fraction  = 0.50;  // fraction of drawdown that must recover
        int    sigma_window       = 30;    // rolling window for σ estimation
    };

    // prices and ticks must have the same length.
    // Returns all detected flash-crash events, sorted by start_tick.
    static std::vector<FlashCrashEvent> detect(
        const std::vector<double>&   prices,
        const std::vector<uint64_t>& ticks,
        const Config& cfg);
};
