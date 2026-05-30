// Avellaneda & Stoikov (2008) + Glosten-Milgrom (1985) Bayesian belief update from OFI.
// r = μ̂ - q·γ·σ²·(T-t),  δ = γ·σ²·(T-t) + (2/γ)·ln(1+γ/κ) + adverse_sel·|ofi_tick|
#include "MarketMakerAS.h"
#include <cmath>
#include <algorithm>

MarketMakerAS::MarketMakerAS(AgentId id, const std::string& ticker,
                             std::mt19937_64 rng, Params p,
                             RiskLimits rl, LatencyProfile lp, InformationProfile ip)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
{}

std::vector<Action> MarketMakerAS::on_market_data(const AgentSnapshot& snap) {
    if (snap.base.spread < 0) return {};  // invalid book

    double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    if (first_tick_) {
        birth_tick_ = snap.base.tick;
        belief_     = mid;
        first_tick_ = false;
    }

    // Glosten-Milgrom: belief reverts toward perceived mid and shifts with OFI signal.
    // buy pressure (ofi_tick > 0) → raise estimate; sell pressure → lower estimate.
    double ofi = snap.base.ofi_tick;
    belief_ = (1.0 - p_.belief_decay) * belief_
            + p_.belief_decay * mid
            + p_.beta_ofi * ofi;

    double elapsed     = static_cast<double>(snap.base.tick - birth_tick_);
    double remaining_T = std::max(1.0, p_.T - elapsed);

    // Real inventory from ledger (via AgentRunner → AgentSnapshot::own_inventory).
    double q = snap.own_inventory;

    // A-S reservation price around private belief.
    double reservation = belief_ - q * p_.gamma * p_.sigma * p_.sigma * remaining_T;

    // A-S half-spread + adverse selection term: widens when order flow is toxic.
    double spread_half = 0.5 * (p_.gamma * p_.sigma * p_.sigma * remaining_T
                                + (2.0 / p_.gamma) * std::log(1.0 + p_.gamma / p_.kappa))
                       + p_.adverse_sel * std::abs(ofi);

    // Regime-adaptive minimum half-spread proportional to realized volatility (Fact 7).
    constexpr double kVolBaseline = 0.003;
    double rv = snap.base.realized_vol[0];
    if (rv <= 0.0) rv = p_.sigma;
    double min_half = std::max(1.0, rv / kVolBaseline);
    spread_half = std::max(spread_half, min_half);

    Price bid_px = static_cast<Price>(std::floor(reservation - spread_half));
    Price ask_px = static_cast<Price>(std::ceil (reservation + spread_half));
    if (bid_px <= 0 || ask_px <= bid_px) return {};

    // TTL = tick + 2: two generations of orders survive the expire step so the
    // publisher always sees a populated book (expire removes tick-2 orders after publish).
    Tick ttl = snap.base.tick + 2;

    return {
        submit(Side::Buy,  OrderType::Limit, bid_px, p_.qty, ttl),
        submit(Side::Sell, OrderType::Limit, ask_px, p_.qty, ttl),
    };
}
