#include <catch_amalgamated.hpp>
#include "core/Clock.h"
#include <vector>
#include <chrono>

TEST_CASE("Clock AsFastAsPossible — fires all ticks in order", "[clock]") {
    Clock clk(ClockMode::AsFastAsPossible, 1.0);
    std::vector<Tick> fired;
    clk.run(100, [&](Tick t) { fired.push_back(t); });

    REQUIRE(fired.size() == 100);
    for (size_t i = 0; i < fired.size(); ++i)
        REQUIRE(fired[i] == static_cast<Tick>(i));
}

TEST_CASE("Clock AsFastAsPossible — 10k ticks completes quickly", "[clock]") {
    Clock clk(ClockMode::AsFastAsPossible, 0.001);
    Tick count = 0;
    auto t0 = std::chrono::steady_clock::now();
    clk.run(10'000, [&](Tick) { ++count; });
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    REQUIRE(count == 10'000);
    REQUIRE(elapsed_ms < 2000); // must finish well under 2s
}

TEST_CASE("Clock Accelerated — runs faster than real-time", "[clock]") {
    // 10 ticks × 0.05s = 0.5s real-time; accelerated ×10 → ~0.05s wall-clock
    Clock clk(ClockMode::Accelerated, 0.05, 10.0);
    Tick count = 0;
    auto t0 = std::chrono::steady_clock::now();
    clk.run(10, [&](Tick) { ++count; });
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    REQUIRE(count == 10);
    REQUIRE(elapsed_ms < 300); // should take ~50ms, give generous headroom
}

TEST_CASE("Clock current_tick reflects last tick run", "[clock]") {
    Clock clk(ClockMode::AsFastAsPossible, 1.0);
    clk.run(7, [](Tick){});
    // After run(N) completes the loop variable reaches N (termination condition).
    // During the loop on_tick(t) received values 0..6.
    REQUIRE(clk.current_tick() == 7);
}
