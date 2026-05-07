// Avellaneda & Stoikov (2008): r = s - q·γ·σ²·(T-t), δ = γ·σ²·(T-t) + (2/γ)·ln(1+γ/κ)
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

    if (first_tick_) {
        birth_tick_ = snap.base.tick;
        first_tick_ = false;
    }

    double elapsed = static_cast<double>(snap.base.tick - birth_tick_);
    double remaining_T = std::max(1.0, p_.T - elapsed);

    double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    // Net inventory in lots (read from snapshot if fundamental provided; else assume 0 start)
    double q = 0.0;  // inventory neutral by default; Phase 6 wires up ledger feedback

    double reservation = mid - q * p_.gamma * p_.sigma * p_.sigma * remaining_T;
    double spread_half  = 0.5 * (p_.gamma * p_.sigma * p_.sigma * remaining_T
                                 + (2.0 / p_.gamma) * std::log(1.0 + p_.gamma / p_.kappa));
    spread_half = std::max(spread_half, 1.0);  // minimum 1 tick spread

    Price bid_px = static_cast<Price>(std::floor(reservation - spread_half));
    Price ask_px = static_cast<Price>(std::ceil (reservation + spread_half));
    if (bid_px <= 0 || ask_px <= bid_px) return {};

    // GTT = current tick + 1: quotes expire next tick so stale quotes never linger.
    Tick ttl = snap.base.tick + 1;

    return {
        submit(Side::Buy,  OrderType::Limit, bid_px, p_.qty, ttl),
        submit(Side::Sell, OrderType::Limit, ask_px, p_.qty, ttl),
    };
}
