#include <catch_amalgamated.hpp>
#include "core/RollingWindow.h"
#include <numeric>
#include <cmath>

TEST_CASE("RollingWindow — mean and stddev on small known sequence", "[rolling]") {
    RollingWindow<double> w(5);
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) w.push(v);

    REQUIRE(w.size() == 5);
    REQUIRE(w.full());
    REQUIRE_THAT(w.mean(), Catch::Matchers::WithinAbs(3.0, 1e-10));

    // Population variance of {1,2,3,4,5} = 2.0
    REQUIRE_THAT(w.variance(), Catch::Matchers::WithinAbs(2.0, 1e-9));
}

TEST_CASE("RollingWindow — eviction preserves correct stats", "[rolling]") {
    RollingWindow<double> w(3);
    w.push(10.0);
    w.push(20.0);
    w.push(30.0);
    // Window: {10,20,30}, mean=20
    REQUIRE_THAT(w.mean(), Catch::Matchers::WithinAbs(20.0, 1e-10));

    w.push(40.0);
    // Window: {20,30,40}, mean=30
    REQUIRE_THAT(w.mean(), Catch::Matchers::WithinAbs(30.0, 1e-10));
    REQUIRE(w.front() == Catch::Approx(20.0));
    REQUIRE(w.back()  == Catch::Approx(40.0));
}

TEST_CASE("RollingWindow — Welford vs naive variance on N=10000", "[rolling]") {
    const int N = 10000;
    RollingWindow<double> w(N);

    // Known sequence: i / 100.0 for i in 0..N-1
    std::vector<double> vals(N);
    for (int i = 0; i < N; ++i) vals[i] = i / 100.0;
    for (double v : vals) w.push(v);

    double naive_mean = std::accumulate(vals.begin(), vals.end(), 0.0) / N;
    double naive_var  = 0.0;
    for (double v : vals) naive_var += (v - naive_mean) * (v - naive_mean);
    naive_var /= N;

    REQUIRE_THAT(w.mean(), Catch::Matchers::WithinRel(naive_mean, 1e-9));
    REQUIRE_THAT(w.variance(), Catch::Matchers::WithinRel(naive_var, 1e-6));
}

TEST_CASE("RollingWindow — sum is consistent with mean*size", "[rolling]") {
    RollingWindow<double> w(10);
    for (double v : {1.0, 3.0, 5.0, 7.0, 9.0}) w.push(v);
    REQUIRE_THAT(w.sum(), Catch::Matchers::WithinAbs(w.mean() * w.size(), 1e-10));
}

TEST_CASE("RollingWindow — reset clears state", "[rolling]") {
    RollingWindow<double> w(5);
    for (double v : {1.0, 2.0, 3.0}) w.push(v);
    w.reset();
    REQUIRE(w.empty());
    REQUIRE(w.size() == 0);
    REQUIRE(w.mean() == Catch::Approx(0.0));
}
