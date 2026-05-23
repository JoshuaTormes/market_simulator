#pragma once
#include "MarketSnapshot.h"
#include "orderbook/OrderBookV2.h"
#include "orderbook/Trade.h"
#include "core/RollingWindow.h"
#include <array>
#include <deque>
#include <cstdint>

// Produces MarketSnapshot each tick from live OrderBook + trade feed.
// Call on_trade() for every trade from MatchingEngine, then publish() once per tick.
class MarketDataPublisher {
public:
    struct Config {
        // Rolling window sizes in ticks
        std::array<int, MarketSnapshot::kWindows> windows = {60, 300, 900};
        // Number of recent returns to carry in the snapshot
        int recent_returns_size = 60;
        // Depth levels to compute (≤ MarketSnapshot::kDepth)
        int depth_levels = MarketSnapshot::kDepth;
    };

    explicit MarketDataPublisher(const OrderBookV2& book, Config cfg);
    explicit MarketDataPublisher(const OrderBookV2& book) : MarketDataPublisher(book, Config{}) {}

    // Feed every trade immediately after it occurs
    void on_trade(const Trade& trade, Tick now);

    // Called once per tick after all trades are processed.
    // Reads current book state and computes all snapshot fields.
    MarketSnapshot publish(Tick now);

    // Set once at startup: initial mid for empty-book carry-forward on tick 0.
    // Must be called before the first publish() call.
    void set_initial_mid(Price p) { prev_valid_mid_ = p; }

private:
    const OrderBookV2& book_;
    Config cfg_;

    // State
    Price last_trade_price_ = 0;
    Tick  last_trade_tick_  = 0;
    Price prev_mid_         = 0;  // mid from previous tick (for return calculation)
    Price prev_valid_mid_   = 0;  // last mid formed by a two-sided book (carry-forward)
    double momentum_prev_   = 0.0;
    double tick_ofi_        = 0.0; // per-tick signed volume accumulator, reset each publish()

    // Per-window rolling stats
    struct WindowState {
        int size;
        RollingWindow<double> returns; // log-returns
        RollingWindow<double> pv;      // price × vol (for VWAP numerator)
        RollingWindow<double> vol;     // volume (for VWAP denominator)
        RollingWindow<double> ofi;     // signed volume (OFI proxy)
        explicit WindowState(int k) : size(k), returns(k), pv(k), vol(k), ofi(k) {}
    };
    std::array<WindowState*, MarketSnapshot::kWindows> windows_{};

    // Recent returns ring buffer (max recent_returns_size entries)
    std::deque<double> recent_returns_;

    // Helpers
    void push_return(double r);
    static Price compute_micro_price(Price best_bid, Qty bid_qty,
                                      Price best_ask, Qty ask_qty);
    static Price compute_weighted_mid(const std::vector<DepthLevel>& bids,
                                       const std::vector<DepthLevel>& asks);
};
