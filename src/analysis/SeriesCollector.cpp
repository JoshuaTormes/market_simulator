#include "SeriesCollector.h"
#include "persistence/RecordBuild.h"
#include <algorithm>
#include <cmath>
#include <utility>

SeriesCollector::SeriesCollector(std::vector<AgentId> mm_ids)
    : mm_ids_(std::move(mm_ids)) {}

bool SeriesCollector::is_mm(AgentId a) const {
    return std::find(mm_ids_.begin(), mm_ids_.end(), a) != mm_ids_.end();
}

void SeriesCollector::consume(const LogRecord& rec) {
    if (auto* sr = std::get_if<MarketSnapshotRecord>(&rec))  on_snapshot(*sr);
    else if (auto* tr = std::get_if<TradeRecord>(&rec))      on_trade(*tr);
    else if (auto* mr = std::get_if<MetaorderRecord>(&rec))  on_metaorder(*mr);
}

void SeriesCollector::write_trade(const Trade& t)              { on_trade(to_record(t)); }
void SeriesCollector::write_snapshot(const MarketSnapshot& s)  {
    if (!std::isnan(s.non_mm_top_frac)) { d_.non_mm_top_sum += s.non_mm_top_frac; ++d_.non_mm_top_n; }
    on_snapshot(to_record(s));
}
void SeriesCollector::write_metaorder(const MetaorderEvent& m) { on_metaorder(to_record(m)); }

void SeriesCollector::on_snapshot(const MarketSnapshotRecord& sr) {
    if (sr.mid_price > 0) {
        d_.mid_prices.push_back(static_cast<double>(sr.mid_price));
        d_.price_ticks.push_back(sr.tick);
        d_.spreads.push_back(static_cast<double>(sr.spread));
        d_.ofi.push_back(sr.ofi_tick);
        d_.fundamental.push_back(sr.fundamental_value);
        d_.news_cum.push_back(sr.news_cum_log);
        d_.volume_per_snap.push_back(pending_volume_);
        pending_volume_ = 0.0;
        if (sr.spread > 0) ++d_.n_two_sided;
        if (sr.fundamental_value > 0.0) d_.has_fundamental = true;
    }
    ++d_.n_snapshots;
}

void SeriesCollector::on_trade(const TradeRecord& tr) {
    const double q = static_cast<double>(tr.qty);
    d_.trade_signs.push_back(tr.taker_side == 0 ? 1.0 : -1.0);
    pending_volume_  += q;
    d_.total_volume  += q;
    if (!mm_ids_.empty()) {
        const bool mk = is_mm(tr.maker_agent);
        const bool tk = is_mm(tr.taker_agent);
        if (mk && tk) d_.mm_volume += q;
        if (mk) d_.volume_by_mm[tr.maker_agent] += q;
        if (tk) d_.volume_by_mm[tr.taker_agent] += q;
    }
    ++d_.n_trades;
}

void SeriesCollector::on_metaorder(const MetaorderRecord& mr) {
    LogData::Metaorder m;
    m.tick_start    = mr.tick_start;
    m.tick_end      = mr.tick_end;
    m.agent         = mr.agent_id;
    m.sign          = mr.side == 0 ? 1 : -1;
    m.qty_requested = static_cast<double>(mr.qty_requested);
    m.qty_submitted = static_cast<double>(mr.qty_submitted);
    m.qty_executed  = static_cast<double>(mr.qty_executed);
    d_.metaorders.push_back(m);
}

LogData SeriesCollector::finish() {
    // news_cum is only usable if every kept snapshot carried it (v4 log).
    d_.has_news_cum = !d_.news_cum.empty()
        && std::none_of(d_.news_cum.begin(), d_.news_cum.end(),
                        [](double x) { return std::isnan(x); });
    if (!d_.has_news_cum) d_.news_cum.clear();
    LogData out = std::move(d_);
    d_ = LogData{};
    pending_volume_ = 0.0;
    return out;
}
