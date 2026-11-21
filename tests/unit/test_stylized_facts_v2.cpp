// Synthetic-series tests for the v2 battery.  Each criterion has to reject
// the series that lacks the fact — a criterion white noise passes is free.
#include <catch_amalgamated.hpp>
#include "analysis/StylizedFactsV2.h"
#include <cmath>
#include <random>

using namespace sfv2;

namespace {

// Wrap a return series as if it came from `slots_per_day` buckets per session.
Aggregated wrap(std::vector<double> r, int slots_per_day, int horizon,
                std::vector<double> vol = {}) {
    Aggregated a;
    a.horizon_ticks = horizon;
    a.ret = std::move(r);
    a.volume = vol.empty() ? std::vector<double>(a.ret.size(), 1.0) : std::move(vol);
    for (size_t i = 0; i < a.ret.size(); ++i) {
        a.slot.push_back(static_cast<int>(i % slots_per_day));
        a.day.push_back(static_cast<int64_t>(i / slots_per_day));
    }
    return a;
}

std::vector<double> white_noise(size_t n, uint64_t seed) {
    std::mt19937_64 e(seed);
    std::normal_distribution<double> z(0.0, 1e-3);
    std::vector<double> r(n);
    for (auto& v : r) v = z(e);
    return r;
}

// GARCH(1,1) with unit-variance Student-t(5) innovations.
std::vector<double> garch_t(size_t n, uint64_t seed) {
    std::mt19937_64 e(seed);
    std::student_t_distribution<double> t(5.0);
    const double scale = std::sqrt(3.0 / 5.0);
    const double a = 0.10, b = 0.85, w = 1e-6 * (1.0 - a - b);
    double h = 1e-6, r_prev = 0.0;
    std::vector<double> r(n);
    for (size_t i = 0; i < n + 1000; ++i) {
        h = w + a * r_prev * r_prev + b * h;
        const double ri = std::sqrt(h) * t(e) * scale;
        if (i >= 1000) r[i - 1000] = ri;
        r_prev = ri;
    }
    return r;
}

// EGARCH(1,1) with a negative leverage term θ: bad news raises vol.
std::vector<double> egarch(size_t n, double theta, uint64_t seed) {
    std::mt19937_64 e(seed);
    std::normal_distribution<double> z(0.0, 1.0);
    const double beta = 0.97, alpha = 0.15, Eabs = std::sqrt(2.0 / M_PI);
    const double omega = (1.0 - beta) * std::log(1e-6);
    double lh = std::log(1e-6), zp = 0.0;
    std::vector<double> r(n);
    for (size_t i = 0; i < n + 1000; ++i) {
        lh = omega + beta * lh + alpha * (std::abs(zp) - Eabs) + theta * zp;
        const double zi = z(e);
        if (i >= 1000) r[i - 1000] = std::exp(0.5 * lh) * zi;
        zp = zi;
    }
    return r;
}

}  // namespace

TEST_CASE("aggregate: buckets inside a session, no overnight return", "[sfv2]") {
    TimeScale ts;   // 23400 ticks per session
    LogData d;
    double m = 10000.0;
    for (uint64_t t = 0; t < 2 * 23400; ++t) {
        m *= (t % 2 == 0) ? 1.0001 : 0.99995;
        d.price_ticks.push_back(t);
        d.mid_prices.push_back(m);
        d.volume_per_snap.push_back(1.0);
    }
    const auto a = aggregate(d, 300, ts);
    REQUIRE(a.ret.size() == 2 * 77);              // 78 buckets → 77 returns per session
    REQUIRE(a.day.front() == 0);
    REQUIRE(a.day.back() == 1);
    REQUIRE(a.slot[77] == 1);                     // first return of day 1 starts at slot 1
    // Return of bucket 1 vs bucket 0: close at tick 599 over close at tick 299.
    const double expect = std::log(d.mid_prices[599] / d.mid_prices[299]);
    REQUIRE(a.ret[0] == Catch::Approx(expect).epsilon(1e-12));
    REQUIRE(a.volume[0] == Catch::Approx(300.0));
}

TEST_CASE("white noise: fat tails, clustering and long memory are rejected", "[sfv2]") {
    const auto r5 = wrap(white_noise(20000, 1), 78, 300);
    const auto r1 = wrap(white_noise(60000, 2), 390, 60);
    CHECK(fat_tails(r5).status == Status::Fail);
    CHECK(vol_clustering(r5).status == Status::Fail);
    CHECK(vol_long_memory(r1, 390).status == Status::Fail);
    CHECK(no_autocorrelation(r1, r5).status == Status::Pass);
    CHECK(leverage(r5).status == Status::Fail);
    CHECK(gain_loss_asymmetry(wrap(white_noise(5000, 3), 13, 1800)).status
          == Status::Inconclusive);
}

TEST_CASE("GARCH(1,1)-t: fat tails and clustering pass", "[sfv2]") {
    const auto r5 = wrap(garch_t(20000, 7), 78, 300);
    const auto ft = fat_tails(r5);
    CHECK(ft.status == Status::Pass);
    CHECK(ft.ci_lo > 0.0);
    CHECK(vol_clustering(r5).status == Status::Pass);
    CHECK(no_autocorrelation(wrap(garch_t(60000, 8), 390, 60), r5).status == Status::Pass);
}

TEST_CASE("autocorrelated returns are rejected", "[sfv2]") {
    std::vector<double> e = white_noise(60001, 4), r(60000);
    for (size_t i = 0; i < r.size(); ++i) r[i] = e[i + 1] + 0.1 * e[i];   // MA(1)
    const auto r1 = wrap(r, 390, 60);
    CHECK(no_autocorrelation(r1, wrap(white_noise(20000, 5), 78, 300)).status == Status::Fail);
}

TEST_CASE("aggregational gaussianity: GARCH kurtosis falls with the horizon", "[sfv2]") {
    const auto g = garch_t(390 * 400, 11);
    const auto sum_of = [&](int k) {
        std::vector<double> s;
        for (size_t i = 0; i + k <= g.size(); i += k) {
            double v = 0; for (int j = 0; j < k; ++j) v += g[i + j]; s.push_back(v);
        }
        return s;
    };
    const auto f = aggregational_gaussianity(wrap(g, 390, 60), wrap(sum_of(5), 78, 300),
                                             wrap(sum_of(30), 13, 1800));
    CHECK(f.status == Status::Pass);
}

TEST_CASE("leverage: EGARCH with negative theta passes, symmetric GARCH does not", "[sfv2]") {
    CHECK(leverage(wrap(egarch(20000, -0.15, 21), 78, 300)).status == Status::Pass);
    CHECK(leverage(wrap(garch_t(20000, 22), 78, 300)).status == Status::Fail);
}

TEST_CASE("long memory: ARFIMA(0, 0.3, 0) log-vol recovers beta = 1 - 2d", "[sfv2]") {
    // x = Σ ψ_j ε_{t-j}, ψ_j = Γ(j+d)/(Γ(j+1)Γ(d)); ρ_x(k) ∝ k^{2d-1}.
    const double d = 0.3;
    const size_t n = 200000, M = 3000;
    std::vector<double> psi(M);
    psi[0] = 1.0;
    for (size_t j = 1; j < M; ++j) psi[j] = psi[j - 1] * (double(j) - 1.0 + d) / double(j);
    std::mt19937_64 e(31);
    std::normal_distribution<double> z(0.0, 1.0);
    std::vector<double> eps(n + M);
    for (auto& v : eps) v = z(e);
    std::vector<double> r(n);
    for (size_t t = 0; t < n; ++t) {
        double x = 0.0;
        for (size_t j = 0; j < M; ++j) x += psi[j] * eps[t + M - j];
        r[t] = 1e-3 * std::exp(0.5 * x) * z(e);
    }
    const auto f = vol_long_memory(wrap(r, 390, 60), 390);
    INFO("beta = " << f.value << "  " << f.note);
    CHECK(f.status == Status::Pass);
    CHECK(f.value == Catch::Approx(1.0 - 2.0 * d).margin(0.1));
}

TEST_CASE("trade-sign memory: Pareto metaorders give gamma = alpha - 1", "[sfv2]") {
    // Lillo-Mike-Farmer: runs of one sign with P(L >= l) = l^-alpha produce a
    // sign ACF ∝ k^{1-alpha}.  alpha = 1.5 → gamma = 0.5.
    const double alpha = 1.5;
    std::mt19937_64 e(41);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<double> s;
    const size_t n = 2'000'000;
    s.reserve(n);
    while (s.size() < n) {
        const double L = std::floor(std::pow(1.0 - u(e), -1.0 / alpha));
        const double sg = u(e) < 0.5 ? 1.0 : -1.0;
        for (double i = 0; i < L && s.size() < n; ++i) s.push_back(sg);
    }
    const auto f = trade_sign_memory(s, 1000);
    INFO("gamma = " << f.value << "  " << f.note);
    CHECK(f.status == Status::Pass);
    CHECK(f.value == Catch::Approx(alpha - 1.0).margin(0.15));

    std::vector<double> iid(n);
    for (auto& v : iid) v = u(e) < 0.5 ? 1.0 : -1.0;
    CHECK(trade_sign_memory(iid, 1000).status == Status::Fail);
}

TEST_CASE("tail index, gain-loss and vol-volume on constructed series", "[sfv2]") {
    std::mt19937_64 e(51);
    std::student_t_distribution<double> t3(3.0);
    std::vector<double> r(20000);
    for (auto& v : r) v = 1e-3 * t3(e);
    const auto th = tail_index(wrap(r, 78, 300));
    INFO("alpha = " << th.value);
    CHECK(th.status == Status::Pass);

    // Negative skew: minus a centred exponential.
    std::exponential_distribution<double> ex(1.0);
    std::vector<double> sk(3000);
    for (auto& v : sk) v = -(ex(e) - 1.0) * 1e-3;
    CHECK(gain_loss_asymmetry(wrap(sk, 13, 1800)).status == Status::Pass);
    std::vector<double> sk_pos(sk);
    for (auto& v : sk_pos) v = -v;
    CHECK(gain_loss_asymmetry(wrap(sk_pos, 13, 1800)).status == Status::Fail);

    // Volume driven by |r| vs independent volume.
    auto g = garch_t(20000, 52);
    std::vector<double> v_dep(g.size()), v_ind(g.size());
    std::normal_distribution<double> nz(0.0, 1.0);
    for (size_t i = 0; i < g.size(); ++i) {
        v_dep[i] = 100.0 + 5e4 * std::abs(g[i]) + 5.0 * nz(e);
        v_ind[i] = 100.0 + 5.0 * nz(e);
    }
    CHECK(vol_volume(wrap(g, 78, 300, v_dep)).status == Status::Pass);
    CHECK(vol_volume(wrap(g, 78, 300, v_ind)).status == Status::Fail);
}
