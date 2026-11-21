#pragma once
// Where the simulation loop sends what it would log.  BinaryLogWriter puts it
// on disk; SeriesCollector keeps the series the report needs in memory, so a
// long ensemble does not have to write and re-read gigabytes of log.
#include "core/Types.h"
#include "orderbook/Trade.h"
#include "marketdata/MarketSnapshot.h"
#include "economics/NewsEvent.h"
#include "agents/MetaorderEvent.h"

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write_trade        (const Trade& t)              = 0;
    virtual void write_snapshot     (const MarketSnapshot& s)     = 0;
    virtual void write_news         (const NewsEvent& n)          = 0;
    virtual void write_regime_change(Tick tick, int old_regime, int new_regime) = 0;
    virtual void write_metaorder    (const MetaorderEvent& m)     = 0;
};

// Fan one stream out to two sinks (e.g. disk and memory).
class TeeLogSink : public ILogSink {
public:
    TeeLogSink(ILogSink& a, ILogSink& b) : a_(a), b_(b) {}
    void write_trade(const Trade& t) override              { a_.write_trade(t);    b_.write_trade(t); }
    void write_snapshot(const MarketSnapshot& s) override  { a_.write_snapshot(s); b_.write_snapshot(s); }
    void write_news(const NewsEvent& n) override           { a_.write_news(n);     b_.write_news(n); }
    void write_regime_change(Tick t, int o, int n) override {
        a_.write_regime_change(t, o, n); b_.write_regime_change(t, o, n);
    }
    void write_metaorder(const MetaorderEvent& m) override { a_.write_metaorder(m); b_.write_metaorder(m); }
private:
    ILogSink& a_;
    ILogSink& b_;
};
