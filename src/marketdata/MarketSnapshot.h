#pragma once
#include "core/Types.h"
#include "orderbook/OrderBookV2.h"
#include <array>
#include <limits>
#include <vector>

// All price fields in integer ticks. Ratios/stats in double.
// Published once per tick by MarketDataPublisher after matching is done.
struct MarketSnapshot {
    Tick tick = 0;

    // ── Book-derived prices ───────────────────────────────────────────────
    Price last_trade_price = 0;
    Price mid_price        = 0;   // (best_bid + best_ask) / 2
    Price micro_price      = 0;   // size-weighted: (ask·bid_qty + bid·ask_qty) / (bid_qty+ask_qty)
    Price weighted_mid     = 0;   // book-weighted across kDepth levels
    Price spread           = 0;   // best_ask - best_bid
    double relative_spread = 0.0; // spread / mid_price

    // ── Depth (top-5 levels) ───────────────────────────────────────────────
    static constexpr int kDepth = 5;
    std::array<DepthLevel, kDepth> bid_levels{};
    std::array<DepthLevel, kDepth> ask_levels{};
    // Cumulative qty and imbalance at each depth cut
    std::array<Qty,    kDepth> bid_cum_qty{};
    std::array<Qty,    kDepth> ask_cum_qty{};
    std::array<double, kDepth> book_imbalance{}; // (bid_cum - ask_cum) / (bid_cum + ask_cum)

    // ── Rolling windows (3 sizes: short / mid / long) ─────────────────────
    static constexpr int kWindows = 3;
    std::array<double, kWindows> realized_vol{}; // sqrt(mean(r²)) per window
    std::array<double, kWindows> vwap{};         // Σ(p·q)/Σ(q) per window

    // ── Flow / microstructure ─────────────────────────────────────────────
    double trade_imbalance      = 0.5; // buy-vol / total-vol over short window (0.0–1.0)
    double order_flow_imbalance = 0.0; // windowed signed volume: windows_[0] mean (short window)
    double ofi_tick             = 0.0; // per-tick OFI: Σbuy_vol − Σsell_vol in this tick only

    // ── Momentum ──────────────────────────────────────────────────────────
    std::vector<double> recent_returns;   // last N log-returns (newest last)
    double momentum     = 0.0;            // (last_price - price_K_ago) / price_K_ago
    double acceleration = 0.0;            // momentum_now - momentum_prev

    // ── Time ──────────────────────────────────────────────────────────────
    Tick time_since_last_trade = 0;

    // Regime from fundamental process (-1 = not applicable, set by SimulationLoop).
    int regime = -1;

    // Latent fundamental value in tick-units, set by SimulationLoop.  Never
    // visible to agents (they read it only via sees_fundamental); logged so
    // price discovery can be measured offline.
    double fundamental_value = 0.0;

    // Σ log-return of the announcements applied to the fundamental so far, set
    // by SimulationLoop.  Logged only: log V minus this is the diffusion part.
    double news_cum_log = 0.0;

    // Share of the resting quantity at the best bid + best ask that belongs to
    // agents other than the market makers.  NaN when one side is empty or the
    // publisher was not told who the makers are.  Not logged.
    double non_mm_top_frac = std::numeric_limits<double>::quiet_NaN();

    bool is_valid() const { return spread >= 0; }
};
