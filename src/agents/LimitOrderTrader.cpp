#include "LimitOrderTrader.h"
#include <algorithm>
#include <cmath>

LimitOrderTrader::LimitOrderTrader(AgentId id, const std::string& ticker,
                                   std::mt19937_64 rng, Params p,
                                   RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , size_dist_(p.size_mu, p.size_sigma)
{
    phi_ = (p_.view_half_life > 0.0) ? std::pow(0.5, 1.0 / p_.view_half_life) : 0.0;
}

std::vector<Action> LimitOrderTrader::on_market_data(const AgentSnapshot& snap) {
    // Every draw is taken every tick, whatever the state, so the stream does
    // not depend on the path (CLAUDE.md: draw always, use or discard after).
    const double u_act  = u01_(rng_);
    const double xi     = n01_(rng_);   // innovation of the view error
    const double eps    = n01_(rng_);   // idiosyncratic opinion
    const double u_k    = u01_(rng_);
    const double raw_sz = size_dist_(rng_);

    // The view error evolves every tick, acting or not.
    if (!view_init_) { view_err_ = p_.view_sigma * xi; view_init_ = true; }
    else view_err_ = phi_ * view_err_ + std::sqrt(std::max(0.0, 1.0 - phi_ * phi_))
                                      * p_.view_sigma * xi;

    const double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};
    const double lm = std::log(mid);
    log_mid_.push_back(lm);
    const size_t L = static_cast<size_t>(std::max(1, p_.chart_window));
    while (log_mid_.size() > L + 1) log_mid_.pop_front();

    if (u_act >= p_.p_act * p_.intraday.multiplier(snap.base.tick)) return {};

    const double tau = static_cast<double>(std::max(1, p_.horizon_ticks));

    double F = 0.0;
    if (snap.has_fundamental && snap.fundamental_value > 0.0) {
        const double S = snap.fundamental_value * std::exp(view_err_);
        F = std::log(S / mid) * std::min(1.0, tau / std::max(1.0, p_.fund_reversion_ticks));
    }
    const double rbar = (log_mid_.size() > 1)
        ? (log_mid_.back() - log_mid_.front()) / static_cast<double>(log_mid_.size() - 1)
        : 0.0;
    const double C = rbar * tau;
    const double sig = snap.base.realized_vol[MarketSnapshot::kWindows - 1];
    const double N = eps * sig * std::sqrt(tau);

    const double wsum = p_.g_fund + std::abs(p_.g_chart) + p_.g_noise;
    if (wsum <= 0.0) return {};
    const double e_hat = (p_.g_fund * F + p_.g_chart * C + p_.g_noise * N) / wsum;
    if (e_hat == 0.0) return {};

    const Side side = (e_hat > 0.0) ? Side::Buy : Side::Sell;
    const double p_hat = mid * std::exp(e_hat);
    const double k = p_.k_max * u_k;

    Price price;
    if (side == Side::Buy) {
        price = static_cast<Price>(std::floor(p_hat * (1.0 - k)));
        const Price ask = snap.base.ask_levels[0].qty > 0 ? snap.base.ask_levels[0].price : 0;
        if (p_.never_cross && ask > 0) price = std::min(price, ask - 1);
    } else {
        price = static_cast<Price>(std::ceil(p_hat * (1.0 + k)));
        const Price bid = snap.base.bid_levels[0].qty > 0 ? snap.base.bid_levels[0].price : 0;
        if (p_.never_cross && bid > 0) price = std::max(price, bid + 1);
    }
    if (price <= 0) return {};

    // Size: lognormal, faded on the side that grows |q|.
    const double q = snap.own_inventory;
    const bool grows = (side == Side::Buy) ? (q >= 0.0) : (q <= 0.0);
    double fade = 1.0;
    if (grows && p_.q_soft > 0.0) fade = std::max(0.0, 1.0 - std::abs(q) / p_.q_soft);
    const Qty qty = std::min<Qty>(p_.max_size, static_cast<Qty>(std::floor(raw_sz * fade)));
    if (qty <= 0) return {};

    return { submit(side, OrderType::Limit, price, qty,
                    snap.base.tick + static_cast<Tick>(tau)) };
}
