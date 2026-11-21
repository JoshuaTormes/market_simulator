// TWAP with Pareto-distributed parent and child sizes: heavy-tailed trade flow
// → fat-tailed returns and persistent trade-sign autocorrelation.
#include "InstitutionalExecutor.h"
#include <algorithm>
#include <cmath>

InstitutionalExecutor::InstitutionalExecutor(AgentId id, const std::string& ticker,
                                             std::mt19937_64 rng, Params p,
                                             RiskLimits rl, LatencyProfile lp, InformationProfile ip,
                                             EventBus* bus)
    : AgentBase(id, ticker, std::move(rng), rl, lp, ip)
    , p_(p)
    , u_(0.0, 0.9999)
    , bus_(bus)
{}

void InstitutionalExecutor::publish_metaorder(double inventory_now) {
    if (!bus_) return;
    const double moved = inventory_now - parent_start_inv_;
    MetaorderEvent ev;
    ev.agent         = id();
    ev.side          = side_;
    ev.tick_start    = parent_start_tick_;
    ev.tick_end      = last_child_tick_;
    ev.qty_requested = parent_requested_;
    ev.qty_submitted = parent_submitted_;
    ev.qty_executed  = static_cast<Qty>(std::llround(side_ == Side::Buy ? moved : -moved));
    bus_->publish(ev);
}

Qty InstitutionalExecutor::draw_parent_qty() {
    const double lo = static_cast<double>(std::max<Qty>(p_.parent_min, 1));
    const double hi = static_cast<double>(std::max<Qty>(p_.parent_max, p_.parent_min));
    if (p_.parent_alpha <= 0.0) return static_cast<Qty>(lo);
    // Inverse-CDF of Pareto(lo, alpha), then truncate at hi.  Truncating rather
    // than rejecting keeps the RNG stream one draw per parent, which the
    // determinism tests depend on.
    const double q = lo * std::pow(1.0 - u_(rng_), -1.0 / p_.parent_alpha);
    return static_cast<Qty>(std::llround(std::min(q, hi)));
}

std::vector<Action> InstitutionalExecutor::on_market_data(const AgentSnapshot& snap) {
    const Tick now = snap.base.tick;

    // The previous parent's last child has been filled (or rejected) by now.
    if (pending_close_) {
        publish_metaorder(static_cast<double>(snap.own_inventory));
        pending_close_ = false;
    }

    // Idle: maybe a new parent order arrives.  The Bernoulli draw happens every
    // idle tick so the arrival process is memoryless in tick time.
    if (remaining_ <= 0) {
        const double lam = std::clamp(p_.arrival_lambda, 0.0, 1.0)
                         * p_.intraday.multiplier(now);
        if (u_(rng_) >= lam) return {};
        // Tilt the parent side against the book the desk is already carrying.
        const double qs = (p_.q_scale > 0.0) ? p_.q_scale : 1.0;
        const double p_buy = 0.5 - 0.5 * std::tanh(snap.own_inventory / qs);
        side_        = (u_(rng_) < p_buy) ? Side::Buy : Side::Sell;
        remaining_   = draw_parent_qty();
        slices_done_ = 0;
        next_tick_   = now;
        ++parents_started_;
        parent_start_tick_ = now;
        last_child_tick_   = now;
        parent_requested_  = remaining_;
        parent_submitted_  = 0;
        parent_start_inv_  = static_cast<double>(snap.own_inventory);
    }

    if (now < next_tick_) return {};

    int slices_left = p_.total_slices - slices_done_;
    if (slices_left <= 0) {           // schedule exhausted: drop the residual
        remaining_ = 0;
        // The last child went out ticks_between ago, so its fill is visible.
        publish_metaorder(static_cast<double>(snap.own_inventory));
        return {};
    }

    Qty base_qty = std::max<Qty>(remaining_ / slices_left, 1);

    Qty child_qty = base_qty;
    if (p_.pareto_alpha > 0.0) {
        // Pareto(1, alpha) draw — unbiased multiplier: E = 1 when alpha > 1.
        double multiplier = ((p_.pareto_alpha - 1.0) / p_.pareto_alpha)
                          * std::pow(1.0 - u_(rng_), -1.0 / p_.pareto_alpha);
        child_qty = static_cast<Qty>(std::max(1.0, std::round(
                        std::min(static_cast<double>(p_.max_child_qty),
                                 static_cast<double>(base_qty) * multiplier))));
    }
    child_qty = std::min(child_qty, remaining_);

    remaining_ -= child_qty;
    ++slices_done_;
    next_tick_  = now + static_cast<Tick>(p_.ticks_between);
    parent_submitted_ += child_qty;
    last_child_tick_   = now;
    if (remaining_ <= 0) pending_close_ = true;

    return { submit(side_, OrderType::Market, 0, child_qty) };
}
