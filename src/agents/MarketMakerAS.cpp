// reservation = belief - inventory_skew_ticks · (q / q_soft)
// half        = half_spread_min_ticks + vol_mult · sigma_ticks
//               + adverse_sel_ticks_per_lot · |OFI_tick|
// size(side that grows |q|) = qty · max(0, 1 - |q| / q_soft)
#include "MarketMakerAS.h"
#include <algorithm>
#include <cmath>

MarketMakerAS::MarketMakerAS(AgentId id, const std::string& ticker,
                             std::mt19937_64 rng, Params p,
                             RiskLimits rl, LatencyProfile lp,
                             InformationProfile ip, EventBus* bus)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
{
    if (bus) {
        bus->subscribe<NewsEvent>([this](const NewsEvent& ev) {
            // A public announcement is public: the maker reprices immediately
            // rather than waiting to be picked off by whoever read it first.
            pending_news_ += ev.impact_log_return;
        });
    }
}

std::vector<Action> MarketMakerAS::on_market_data(const AgentSnapshot& snap) {
    const double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    if (first_tick_) {
        belief_     = mid;
        first_tick_ = false;
    }

    // ── Belief update (Glosten-Milgrom) ──────────────────────────────────
    if (pending_news_ != 0.0) {
        belief_ *= std::exp(pending_news_);
        pending_news_ = 0.0;
    }

    const double ofi = snap.base.ofi_tick;
    belief_ += p_.lambda_kyle * ofi;

    const double last = static_cast<double>(snap.perceived_last);
    if (last > 0.0)
        belief_ += p_.belief_decay * (last - belief_);

    // Belief is a price: an unbounded random walk in the belief would let the
    // maker quote a nonsensical level after a long one-sided stretch, so keep
    // it within a wide band of the observable mid.
    const double band = std::max(50.0, 0.10 * mid);
    belief_ = std::clamp(belief_, mid - band, mid + band);

    // ── Quote geometry, in ticks ──────────────────────────────────────────
    const double rv          = std::max(snap.base.realized_vol[0], p_.sigma_tick_floor);
    const double sigma_ticks = rv * mid;
    const double half = p_.half_spread_min_ticks
                      + p_.vol_mult * sigma_ticks
                      + p_.adverse_sel_ticks_per_lot * std::abs(ofi);

    const double q      = snap.own_inventory;
    const double q_soft = std::max(1.0, p_.q_soft);
    const double reservation = belief_ - p_.inventory_skew_ticks * (q / q_soft);

    Price bid_px = static_cast<Price>(std::floor(reservation - half));
    Price ask_px = static_cast<Price>(std::ceil (reservation + half));
    if (bid_px <= 0) return {};
    if (ask_px <= bid_px) ask_px = bid_px + 1;

    // ── Size: the side that would grow the position fades out ─────────────
    const double fade = std::max(0.0, 1.0 - std::abs(q) / q_soft);
    const double base = static_cast<double>(p_.qty);
    Qty bid_qty = static_cast<Qty>(std::llround(q > 0.0 ? base * fade : base));
    Qty ask_qty = static_cast<Qty>(std::llround(q < 0.0 ? base * fade : base));

    // ── Requote ───────────────────────────────────────────────────────────
    // CancelAll goes out first so it carries a lower seq than the quotes and
    // is applied before them at the same arrival tick.  The TTL is only a
    // safety net for a cancel lost to latency, so it must sit clear of the
    // requote cycle: the agent observes tick t-1 and the order reaches the
    // book at t, where expire(t) runs before the snapshot is published.  A TTL
    // of t-1+1 = t would therefore delete every quote before anyone could see
    // it, leaving the book permanently empty.
    const Tick ttl = snap.base.tick + 3;

    std::vector<Action> out;
    out.reserve(3);
    out.push_back(cancel_all());
    if (bid_qty > 0) out.push_back(submit(Side::Buy,  OrderType::Limit, bid_px, bid_qty, ttl));
    if (ask_qty > 0) out.push_back(submit(Side::Sell, OrderType::Limit, ask_px, ask_qty, ttl));
    return out;
}
