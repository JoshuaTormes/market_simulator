#include "core/Config.h"
#include "core/RngService.h"
#include "core/Clock.h"
#include "core/Logger.h"
#include <cstdio>

// Minimal wiring stub — expanded incrementally as phases land.
// All heavy logic lives in sim_core; this file only does DI assembly.
int main(int argc, char** argv) {
    SimulationConfig cfg;

    // Parse --seed N from argv
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--seed")
            cfg.seed = static_cast<uint64_t>(std::stoul(argv[i + 1]));
        if (std::string(argv[i]) == "--duration")
            cfg.max_ticks = static_cast<uint64_t>(std::stoul(argv[i + 1]));
    }

    RngService rng(cfg.seed);

    auto stderr_sink = [](const LogEntry& e) {
        const char* lvl = e.level == LogLevel::ERROR ? "ERROR"
                        : e.level == LogLevel::WARN  ? "WARN"
                        : e.level == LogLevel::INFO  ? "INFO"
                        : "DEBUG";
        std::fprintf(stderr, "[%s tick=%llu] %s\n", lvl,
                     (unsigned long long)e.tick, e.msg);
    };
    Logger logger(stderr_sink);

    Clock clk(cfg.clock_mode, cfg.seconds_per_tick, cfg.accel_factor);
    logger.log_info(0, "Simulation starting (stub — Phase 0)");

    clk.run(cfg.max_ticks, [&](Tick t) {
        (void)t;
        // SimulationLoop will be wired here in Phase 6.
    });

    logger.log_info(cfg.max_ticks, "Simulation complete");
    logger.flush();
    return 0;
}
