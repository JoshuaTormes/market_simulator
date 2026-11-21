#pragma once
// Builds the LogData the report needs, either live (as an ILogSink wired into
// SimulationLoop) or from records read back from a .bin.  Both paths go
// through the same packed records and the same consume(), so a report from
// memory and a report from disk cannot drift apart.
#include "Report.h"
#include "persistence/ILogSink.h"
#include "persistence/BinaryLogReader.h"
#include <vector>

class SeriesCollector : public ILogSink {
public:
    // mm_ids: market-maker agent ids, so MM-vs-MM volume can be separated.
    explicit SeriesCollector(std::vector<AgentId> mm_ids = {});

    // From disk.
    void consume(const LogRecord& rec);

    // Live, from the loop.
    void write_trade(const Trade& t) override;
    void write_snapshot(const MarketSnapshot& s) override;
    void write_news(const NewsEvent&) override {}
    void write_regime_change(Tick, int, int) override {}
    void write_metaorder(const MetaorderEvent& m) override;

    // Finalize and hand over the series.  The collector is empty afterwards.
    LogData finish();

private:
    void on_snapshot(const MarketSnapshotRecord& r);
    void on_trade(const TradeRecord& r);
    void on_metaorder(const MetaorderRecord& r);
    bool is_mm(AgentId a) const;

    LogData              d_;
    std::vector<AgentId> mm_ids_;
    double               pending_volume_ = 0.0;
};
