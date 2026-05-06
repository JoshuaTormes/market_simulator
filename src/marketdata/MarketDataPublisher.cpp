#include "MarketDataPublisher.h"
#include <cmath>
#include <algorithm>
#include <cassert>

MarketDataPublisher::MarketDataPublisher(const OrderBookV2& book, Config cfg)
    : book_(book), cfg_(cfg)
{
    for (int i = 0; i < MarketSnapshot::kWindows; ++i) {
        windows_[i] = new WindowState(cfg_.windows[i]);
    }
}

// ── on_trade ──────────────────────────────────────────────────────────────

void MarketDataPublisher::on_trade(const Trade& trade, Tick now) {
    last_trade_price_ = trade.price;
    last_trade_tick_  = now;

    // Lee-Ready: use taker_side directly (we have it from MatchingEngine)
    double signed_vol = (trade.taker_side == Side::Buy)
                      ?  static_cast<double>(trade.qty)
                      : -static_cast<double>(trade.qty);
    double p = static_cast<double>(trade.price);
    double q = static_cast<double>(trade.qty);

    for (int i = 0; i < MarketSnapshot::kWindows; ++i) {
        windows_[i]->pv.push(p * q);
        windows_[i]->vol.push(q);
        windows_[i]->ofi.push(signed_vol);
    }
}

// ── publish ───────────────────────────────────────────────────────────────

MarketSnapshot MarketDataPublisher::publish(Tick now) {
    MarketSnapshot snap;
    snap.tick = now;
    snap.last_trade_price = last_trade_price_;
    snap.time_since_last_trade = (last_trade_tick_ == 0) ? now
                                                          : (now - last_trade_tick_);

    Price best_bid = book_.best_bid();
    Price best_ask = book_.best_ask();

    // ── Prices ────────────────────────────────────────────────────────────
    if (best_bid > 0 && best_ask > 0) {
        snap.mid_price    = (best_bid + best_ask) / 2;
        snap.spread       = best_ask - best_bid;
        snap.relative_spread = (snap.mid_price > 0)
            ? static_cast<double>(snap.spread) / static_cast<double>(snap.mid_price)
            : 0.0;
    } else {
        // One or both sides empty — use last trade as anchor
        snap.mid_price    = last_trade_price_;
        snap.spread       = 0;
        snap.relative_spread = 0.0;
    }

    // Depth vectors
    auto bids_v = book_.top_bids(cfg_.depth_levels);
    auto asks_v = book_.top_asks(cfg_.depth_levels);

    // micro_price (size-weighted top-1)
    {
        Qty bid_qty = bids_v.empty() ? 0 : bids_v[0].qty;
        Qty ask_qty = asks_v.empty() ? 0 : asks_v[0].qty;
        snap.micro_price = compute_micro_price(best_bid, bid_qty, best_ask, ask_qty);
    }

    // weighted_mid (book-weighted across available levels)
    snap.weighted_mid = compute_weighted_mid(bids_v, asks_v);

    // ── Depth levels ──────────────────────────────────────────────────────
    Qty bid_cum = 0, ask_cum = 0;
    int n_levels = std::min(cfg_.depth_levels, static_cast<int>(bids_v.size()));
    for (int i = 0; i < MarketSnapshot::kDepth; ++i) {
        if (i < static_cast<int>(bids_v.size()))
            snap.bid_levels[i] = bids_v[i];
        if (i < static_cast<int>(asks_v.size()))
            snap.ask_levels[i] = asks_v[i];

        bid_cum += (i < static_cast<int>(bids_v.size())) ? bids_v[i].qty : 0;
        ask_cum += (i < static_cast<int>(asks_v.size())) ? asks_v[i].qty : 0;
        snap.bid_cum_qty[i] = bid_cum;
        snap.ask_cum_qty[i] = ask_cum;

        double total = static_cast<double>(bid_cum + ask_cum);
        snap.book_imbalance[i] = (total > 0)
            ? static_cast<double>(bid_cum - ask_cum) / total
            : 0.0;
    }
    (void)n_levels;

    // ── Rolling windows ───────────────────────────────────────────────────

    // Push log-return for this tick
    if (prev_mid_ > 0 && snap.mid_price > 0) {
        double r = std::log(static_cast<double>(snap.mid_price) /
                             static_cast<double>(prev_mid_));
        push_return(r);
        for (int i = 0; i < MarketSnapshot::kWindows; ++i) {
            windows_[i]->returns.push(r);
        }
    }
    prev_mid_ = snap.mid_price;

    for (int i = 0; i < MarketSnapshot::kWindows; ++i) {
        auto& ws = *windows_[i];

        // Realized volatility: sqrt(mean(r²)) = sqrt(variance + mean²)
        double var = ws.returns.variance();
        double mean_r = ws.returns.mean();
        snap.realized_vol[i] = std::sqrt(var + mean_r * mean_r);

        // VWAP: mean(price*vol) / mean(vol)
        double mean_pv  = ws.pv.mean();
        double mean_vol = ws.vol.mean();
        snap.vwap[i] = (mean_vol > 0) ? mean_pv / mean_vol
                                       : static_cast<double>(snap.last_trade_price);

        // OFI: mean signed volume
        snap.order_flow_imbalance = ws.ofi.mean(); // use short window for OFI
    }

    // Trade imbalance (short window): buy_vol / (buy_vol + sell_vol)
    {
        double mean_ofi = windows_[0]->ofi.mean();
        double mean_vol = windows_[0]->vol.mean();
        if (mean_vol > 0.0) {
            double buy_vol_estimate = (mean_vol + mean_ofi) / 2.0;
            snap.trade_imbalance = buy_vol_estimate / mean_vol;
            snap.trade_imbalance = std::max(0.0, std::min(1.0, snap.trade_imbalance));
        } else {
            snap.trade_imbalance = 0.5;
        }
    }

    // ── Momentum / acceleration ───────────────────────────────────────────
    snap.recent_returns.assign(recent_returns_.begin(), recent_returns_.end());

    if (!recent_returns_.empty()) {
        // momentum = (last_price - price_K_ago) / price_K_ago, proxied via cumulative return
        double cumulative = 0.0;
        for (double r : recent_returns_) cumulative += r;
        snap.momentum = std::expm1(cumulative); // e^sum(r) - 1 ≈ sum(r)
    }
    snap.acceleration = snap.momentum - momentum_prev_;
    momentum_prev_    = snap.momentum;

    return snap;
}

// ── Helpers ───────────────────────────────────────────────────────────────

void MarketDataPublisher::push_return(double r) {
    recent_returns_.push_back(r);
    while (static_cast<int>(recent_returns_.size()) > cfg_.recent_returns_size)
        recent_returns_.pop_front();
}

Price MarketDataPublisher::compute_micro_price(Price best_bid, Qty bid_qty,
                                                Price best_ask, Qty ask_qty) {
    Qty total = bid_qty + ask_qty;
    if (total == 0 || best_bid == 0 || best_ask == 0) {
        return (best_bid + best_ask) / 2; // fallback to simple mid
    }
    // micro_price = (best_ask × bid_qty + best_bid × ask_qty) / (bid_qty + ask_qty)
    return static_cast<Price>(
        (static_cast<int64_t>(best_ask) * bid_qty +
         static_cast<int64_t>(best_bid) * ask_qty) / total
    );
}

Price MarketDataPublisher::compute_weighted_mid(const std::vector<DepthLevel>& bids,
                                                 const std::vector<DepthLevel>& asks) {
    if (bids.empty() || asks.empty()) return 0;

    double bid_sum = 0.0, ask_sum = 0.0;
    double bid_w = 0.0, ask_w = 0.0;

    int n = std::min(bids.size(), asks.size());
    for (int i = 0; i < static_cast<int>(n); ++i) {
        double bq = static_cast<double>(bids[i].qty);
        double aq = static_cast<double>(asks[i].qty);
        bid_sum += static_cast<double>(bids[i].price) * bq;
        ask_sum += static_cast<double>(asks[i].price) * aq;
        bid_w   += bq;
        ask_w   += aq;
    }

    if (bid_w == 0.0 || ask_w == 0.0) {
        return (bids[0].price + asks[0].price) / 2;
    }

    // Book-weighted mid: (bid_vwap × ask_w + ask_vwap × bid_w) / (bid_w + ask_w)
    double bid_vwap = bid_sum / bid_w;
    double ask_vwap = ask_sum / ask_w;
    double weighted = (bid_vwap * ask_w + ask_vwap * bid_w) / (bid_w + ask_w);
    return static_cast<Price>(std::round(weighted));
}
