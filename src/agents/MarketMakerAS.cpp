// belief     += lambda_kyle · sign(OFI) · sqrt(|OFI| · q_ref)      (q_ref > 0)
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
    // Drawn on every call, before any state test, so the maker's stream does
    // not depend on the market (it is used only on a random-cadence requote).
    const double gap_u = gap_u_(rng_);
    // Same rule for the news-reading noise: one draw per call, used only on a
    // requote with an announcement pending.
    const double eta = p_.news_signal_noise * eta_n_(news_rng_);
    const double mid = static_cast<double>(snap.perceived_mid);
    if (mid <= 0.0) return {};

    // Off-cadence ticks: the resting quotes stay where they are.  The belief is
    // still updated below only on the ticks the maker acts, which is the point
    // — a maker that cannot requote also cannot act on what it just learned.
    // Signed flow is accumulated on every tick, before the cadence test, so a
    // maker that requotes every 3 ticks prices the whole 3 ticks of flow it
    // missed instead of only the last one.
    if (p_.accumulate_ofi) ofi_acc_ += snap.base.ofi_tick;
    else                   ofi_acc_  = snap.base.ofi_tick;

    const int period = std::max(1, p_.requote_period);
    const Tick now   = snap.base.tick;
    if (!first_tick_) {
        if (p_.random_cadence) { if (now < next_requote_) return {}; }
        else if (((now + p_.requote_phase) % period) != 0) return {};
    }
    // Ticks until the next requote: fixed, or uniform on {1, ..., 2·period−1}.
    Tick gap = static_cast<Tick>(period);
    if (p_.random_cadence) {
        const int span = 2 * period - 1;
        gap = 1 + static_cast<Tick>(std::min(span - 1, static_cast<int>(gap_u * span)));
        next_requote_ = now + gap;
    }

    if (first_tick_) {
        belief_     = mid;
        first_tick_ = false;
    }

    // ── Belief update (fixed-λ flow rule, see header) ────────────────────
    if (pending_news_ != 0.0) {
        if (!p_.news_blind) belief_ *= std::exp(pending_news_ * (1.0 + eta));
        pending_news_ = 0.0;
    }

    // Level scaling (see Params::price_ref_ticks).
    const double lvl = (p_.price_ref_ticks > 0.0) ? mid / p_.price_ref_ticks : 1.0;

    // Flow since the previous requote (see ofi_acc_ above).
    const double ofi = ofi_acc_;
    ofi_acc_ = 0.0;
    double push;
    if (p_.impact_q_ref > 0.0) {
        // Square-root impact: concave in the size of the signed flow, equal to
        // the linear rule at |OFI| = q_ref.
        const double sgn = (ofi > 0.0) ? 1.0 : (ofi < 0.0 ? -1.0 : 0.0);
        push = p_.lambda_kyle * sgn * std::sqrt(std::abs(ofi) * p_.impact_q_ref);
    } else {
        push = p_.lambda_kyle * ofi;
    }
    belief_ += lvl * push;

    const double last = static_cast<double>(snap.perceived_last);
    if (last > 0.0)
        belief_ += p_.belief_decay * (last - belief_);

    // Belief is a price: an unbounded random walk in the belief would let the
    // maker quote a nonsensical level after a long one-sided stretch, so keep
    // it within a wide band of the observable mid.
    const double band = std::max(50.0 * lvl, 0.10 * mid);
    belief_ = std::clamp(belief_, mid - band, mid + band);


    // ── Quote geometry, in ticks ──────────────────────────────────────────
    const double rv          = std::max(snap.base.realized_vol[0], p_.sigma_tick_floor);
    const double sigma_ticks = rv * mid;
    const double half_min = lvl * p_.half_spread_min_ticks;
    const double half = half_min
                      + p_.vol_mult * sigma_ticks
                      + lvl * p_.adverse_sel_ticks_per_lot * std::abs(ofi);

    const double q      = snap.own_inventory;
    const double q_soft = std::max(1.0, p_.q_soft);
    const double reservation = belief_ - lvl * p_.inventory_skew_ticks * (q / q_soft);

    Price bid_px = static_cast<Price>(std::floor(reservation - half));
    Price ask_px = static_cast<Price>(std::ceil (reservation + half));
    if (bid_px <= 0) return {};

    // ── Join the touch when a rival quotes tighter but the floor still holds ──
    // Half the size joins the rival's price, the other half rests at the
    // maker's own level, so the book keeps layers behind the touch instead of
    // stacking every maker at one price with a void behind it.
    Price join_bid_px = 0, join_ask_px = 0;
    if (p_.join_touch) {
        const Price best_bid = snap.base.bid_levels[0].price;
        const Price best_ask = snap.base.ask_levels[0].price;
        const Price bid_cap  = static_cast<Price>(std::floor(reservation - half_min));
        const Price ask_cap  = static_cast<Price>(std::ceil (reservation + half_min));
        if (best_bid > bid_px && best_bid <= bid_cap) join_bid_px = best_bid;
        if (best_ask > 0 && best_ask < ask_px && best_ask >= ask_cap) join_ask_px = best_ask;
    }
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
    const Tick ttl = snap.base.tick + gap + 2;

    std::vector<Action> out;
    out.reserve(5);
    out.push_back(cancel_all());
    auto quote_side = [&](Side side, Price own_px, Price join_px, Qty q) {
        if (q <= 0) return;
        if (join_px > 0 && join_px != own_px && q >= 2) {
            const Qty at_touch = q / 2;
            out.push_back(submit(side, OrderType::Limit, join_px, at_touch, ttl));
            out.push_back(submit(side, OrderType::Limit, own_px, q - at_touch, ttl));
        } else {
            out.push_back(submit(side, OrderType::Limit, own_px, q, ttl));
        }
    };
    quote_side(Side::Buy,  bid_px, join_bid_px, bid_qty);
    quote_side(Side::Sell, ask_px, join_ask_px, ask_qty);
    return out;
}
