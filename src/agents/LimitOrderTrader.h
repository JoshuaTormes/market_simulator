#pragma once
// Limit-order trader with a heterogeneous expectation (Chiarella & Iori 2002).
//
// Every non-maker in the ecology used to trade with market orders, so all the
// resting liquidity came from the three makers and the touch was theirs by
// construction.  Real books are fed by many patient traders who post where
// they think the price is going and wait.
//
// Each trader forms an expected log-return over its own horizon τ,
//
//     ê = [ g_f · F  +  g_c · C  +  g_n · N ] / (g_f + |g_c| + g_n)
//     F = log(S / mid) · min(1, τ / τ_F)         fundamentalist
//     C = r̄_L · τ                                 chartist (g_c < 0: contrarian)
//     N = ε · σ̂ · sqrt(τ),  ε ~ N(0, 1)           idiosyncratic opinion
//
// with S = V · exp(e) a private view of value whose error e is AR(1) — a
// stale opinion that does not average out tick by tick, so these traders are
// far less informed than the informed desks.  r̄_L is the mean 1-tick log-return
// of the mid over the trader's own window L, and σ̂ the realized 1-tick vol the
// trader observes on the tape (not a hand-fixed constant).
//
// Order: p̂ = mid · exp(ê).  p̂ > mid → buy limit at p̂ · (1 − k), capped one
// tick below the best ask so it never crosses; p̂ < mid → sell at p̂ · (1 + k),
// floored one tick above the best bid.  The order rests for τ ticks.  Size is
// lognormal, faded linearly to zero on the side that grows the position as
// |q| → q_soft.  Arrival is Bernoulli per tick with the session profile.
#include "AgentBase.h"
#include "core/IntradayProfile.h"
#include <deque>
#include <random>

class LimitOrderTrader : public AgentBase {
public:
    struct Params {
        double p_act = 0.01;                 // mid-session arrival probability per tick
        IntradayProfile intraday{};
        // Expectation weights (drawn per agent by the factory).
        double g_fund  = 1.0;
        double g_chart = 0.0;
        double g_noise = 1.0;
        int    horizon_ticks   = 300;        // τ: expectation horizon and order TTL
        int    chart_window    = 100;        // L, ticks
        double fund_reversion_ticks = 3600;  // τ_F: time the trader expects the gap to close in
        // Private view of value: log error AR(1) with this stationary std and half-life.
        double view_sigma      = 0.004;
        double view_half_life  = 3600.0;
        double k_max           = 0.002;      // order placed k ~ U(0, k_max) short of p̂
        double size_mu         = 2.0;        // lognormal size
        double size_sigma      = 1.0;
        Qty    max_size        = 100;
        double q_soft          = 500.0;      // growing side faded to 0 at |q| = q_soft
        bool   never_cross     = true;       // cap at best ask − 1 / floor at best bid + 1
    };

    LimitOrderTrader(AgentId id, const std::string& ticker,
                     std::mt19937_64 rng, Params p,
                     RiskLimits rl = {}, LatencyProfile lp = {}, InformationProfile ip = {});

    std::vector<Action> on_market_data(const AgentSnapshot& snap) override;
    const char* type() const override { return "LimitOrderTrader"; }

    double view_error() const { return view_err_; }   // tests and diagnostics

private:
    Params p_;
    double view_err_ = 0.0;     // AR(1) log error of the private view of V
    double phi_      = 0.0;     // AR(1) coefficient per tick
    bool   view_init_ = false;
    std::deque<double> log_mid_; // last L+1 log mids
    std::uniform_real_distribution<double> u01_{0.0, 1.0};
    std::normal_distribution<double>       n01_{0.0, 1.0};
    std::lognormal_distribution<double>    size_dist_;
};
