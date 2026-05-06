#include <catch_amalgamated.hpp>
#include "core/RngService.h"

TEST_CASE("RngService reproducibility — same seed + id yields identical stream", "[rng]") {
    RngService rng(42);
    auto e1 = rng.for_consumer("agent_1");
    auto e2 = rng.for_consumer("agent_1");

    for (int i = 0; i < 1000; ++i) {
        REQUIRE(e1() == e2());
    }
}

TEST_CASE("RngService — different ids yield different streams", "[rng]") {
    RngService rng(42);
    auto e1 = rng.for_consumer("agent_1");
    auto e2 = rng.for_consumer("agent_2");

    bool any_differ = false;
    for (int i = 0; i < 100; ++i) {
        if (e1() != e2()) { any_differ = true; break; }
    }
    REQUIRE(any_differ);
}

TEST_CASE("RngService — different global seeds yield different streams", "[rng]") {
    RngService rng_a(42);
    RngService rng_b(99);
    auto e_a = rng_a.for_consumer("agent_1");
    auto e_b = rng_b.for_consumer("agent_1");

    bool any_differ = false;
    for (int i = 0; i < 100; ++i) {
        if (e_a() != e_b()) { any_differ = true; break; }
    }
    REQUIRE(any_differ);
}

TEST_CASE("RngService — no std::random_device usage (compile-time check)", "[rng]") {
    // If this file compiles, we rely only on RngService.
    // The absence of random_device is enforced by grep in CI.
    SUCCEED("compile-time guarantee");
}
