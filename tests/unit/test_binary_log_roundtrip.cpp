// Round-trip test: write a set of typed records, read them back,
// verify every field survives serialisation + deserialisation unchanged.
#include <catch_amalgamated.hpp>
#include "persistence/BinaryLogWriter.h"
#include "persistence/BinaryLogReader.h"
#include "persistence/EventSchema.h"
#include "orderbook/Trade.h"
#include "marketdata/MarketSnapshot.h"
#include "economics/NewsEvent.h"
#include "analysis/Report.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <filesystem>

static std::string tmp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

TEST_CASE("BinaryLog: file header round-trip", "[binlog]") {
    const std::string path = tmp_path("rt_header.bin");
    {
        BinaryLogWriter w(path, 99u, 5000u, 2u, "TICK");
        REQUIRE(w.is_open());
    }

    BinaryLogReader r(path);
    FileHeaderRecord hdr{};
    REQUIRE(r.read_header(&hdr));
    CHECK(hdr.magic    == kFileMagic);
    CHECK(hdr.version  == kSchemaVersion);
    CHECK(hdr.seed     == 99u);
    CHECK(hdr.max_ticks == 5000u);
    CHECK(hdr.publish_interval == 2u);
    CHECK(std::string(reinterpret_cast<const char*>(hdr.ticker)) == "TICK");
}

TEST_CASE("BinaryLog: trade round-trip", "[binlog]") {
    const std::string path = tmp_path("rt_trade.bin");
    {
        BinaryLogWriter w(path, 1u, 100u, 1u, "T");

        Trade t;
        t.price       = 12345;
        t.qty         = 50;
        t.maker_id    = 7;
        t.taker_id    = 8;
        t.taker_side  = Side::Sell;
        t.maker_agent = 101;
        t.taker_agent = 202;
        t.tick        = 77;
        t.seq_no      = 9;
        t.fee_maker   = -1;
        t.fee_taker   = 3;
        w.write_trade(t);
    }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());
    auto rec = r.next();
    REQUIRE(rec.has_value());
    auto* tr = std::get_if<TradeRecord>(&*rec);
    REQUIRE(tr != nullptr);
    CHECK(tr->price       == 12345);
    CHECK(tr->qty         == 50);
    CHECK(tr->maker_id    == 7);
    CHECK(tr->taker_id    == 8);
    CHECK(tr->taker_side  == 1);   // Sell
    CHECK(tr->maker_agent == 101);
    CHECK(tr->taker_agent == 202);
    CHECK(tr->tick        == 77);
    CHECK(tr->seq_no      == 9);
    CHECK(tr->fee_maker   == -1);
    CHECK(tr->fee_taker   == 3);
}

TEST_CASE("BinaryLog: market snapshot round-trip", "[binlog]") {
    const std::string path = tmp_path("rt_snap.bin");
    {
        BinaryLogWriter w(path, 1u, 100u, 1u, "T");

        MarketSnapshot s;
        s.tick              = 42;
        s.mid_price         = 10050;
        s.spread            = 4;
        s.last_trade_price  = 10048;
        s.realized_vol[0]   = 0.0123;
        s.realized_vol[1]   = 0.0200;
        s.realized_vol[2]   = 0.0350;
        s.vwap[0]           = 10045.5;
        s.ofi_tick             = 0.31;
        s.trade_imbalance   = 0.55;
        s.momentum          = 0.002;
        s.book_imbalance[0] = 0.12;
        s.fundamental_value = 10123.75;
        s.news_cum_log      = -0.0123;
        s.regime            = 1;
        w.write_snapshot(s);
    }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());
    auto rec = r.next();
    REQUIRE(rec.has_value());
    auto* sr = std::get_if<MarketSnapshotRecord>(&*rec);
    REQUIRE(sr != nullptr);
    CHECK(sr->tick              == 42);
    CHECK(sr->mid_price         == 10050);
    CHECK(sr->spread            == 4);
    CHECK(sr->realized_vol_s    == Catch::Approx(0.0123));
    CHECK(sr->vwap_s            == Catch::Approx(10045.5));
    CHECK(sr->ofi_tick             == Catch::Approx(0.31));
    CHECK(sr->fundamental_value == Catch::Approx(10123.75));
    CHECK(sr->news_cum_log      == Catch::Approx(-0.0123));
    CHECK(sr->regime            == 1);
}

// A v2 log has no fundamental_value.  The reader must still open it and widen
// each snapshot into the current layout, leaving the missing field at 0.
TEST_CASE("BinaryLog: v2 snapshots are still readable", "[binlog]") {
    const std::string path = tmp_path("rt_snap_v2.bin");
    {
        FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);

        FileHeaderRecord hdr{};
        hdr.magic   = kFileMagic;
        hdr.version = 2;
        hdr.seed    = 7;
        hdr.max_ticks = 10;
        hdr.publish_interval = 1;
        std::memcpy(hdr.ticker, "OLD", 4);
        uint8_t tag = static_cast<uint8_t>(EventTag::FileHeader);
        std::fwrite(&tag, 1, 1, f);
        std::fwrite(&hdr, sizeof(hdr), 1, f);

        MarketSnapshotRecordV2 old{};
        old.tick             = 11;
        old.mid_price        = 9990;
        old.spread           = 2;
        old.last_trade_price = 9989;
        old.realized_vol_s   = 0.005;
        old.vwap_s           = 9988.5;
        old.ofi_tick         = -1.25;
        old.regime           = 2;
        tag = static_cast<uint8_t>(EventTag::MarketSnapshot);
        std::fwrite(&tag, 1, 1, f);
        std::fwrite(&old, sizeof(old), 1, f);
        std::fclose(f);
    }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());
    CHECK(r.version() == 2);
    auto rec = r.next();
    REQUIRE(rec.has_value());
    auto* sr = std::get_if<MarketSnapshotRecord>(&*rec);
    REQUIRE(sr != nullptr);
    CHECK(sr->tick              == 11);
    CHECK(sr->mid_price         == 9990);
    CHECK(sr->spread            == 2);
    CHECK(sr->last_trade_price  == 9989);
    CHECK(sr->realized_vol_s    == Catch::Approx(0.005));
    CHECK(sr->vwap_s            == Catch::Approx(9988.5));
    CHECK(sr->ofi_tick          == Catch::Approx(-1.25));
    CHECK(sr->regime            == 2);
    CHECK(sr->fundamental_value == 0.0);   // absent in v2
    CHECK(std::isnan(sr->news_cum_log));   // unknown, not zero
}

// A v3 log has fundamental_value but no news_cum_log, and a 112-byte snapshot.
TEST_CASE("BinaryLog: v3 snapshots are still readable", "[binlog]") {
    const std::string path = tmp_path("rt_snap_v3.bin");
    {
        FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        FileHeaderRecord hdr{};
        hdr.magic = kFileMagic; hdr.version = 3; hdr.publish_interval = 1;
        uint8_t tag = static_cast<uint8_t>(EventTag::FileHeader);
        std::fwrite(&tag, 1, 1, f);
        std::fwrite(&hdr, sizeof(hdr), 1, f);

        for (Tick t = 1; t <= 2; ++t) {
            MarketSnapshotRecordV3 old{};
            old.tick              = t;
            old.mid_price         = 10000 + static_cast<Price>(t);
            old.fundamental_value = 10001.5;
            old.regime            = 1;
            tag = static_cast<uint8_t>(EventTag::MarketSnapshot);
            std::fwrite(&tag, 1, 1, f);
            std::fwrite(&old, sizeof(old), 1, f);
        }
        std::fclose(f);
    }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());
    CHECK(r.version() == 3);
    for (Tick t = 1; t <= 2; ++t) {   // the second record proves the stride is 112
        auto rec = r.next();
        REQUIRE(rec.has_value());
        auto* sr = std::get_if<MarketSnapshotRecord>(&*rec);
        REQUIRE(sr != nullptr);
        CHECK(sr->tick              == t);
        CHECK(sr->mid_price         == 10000 + static_cast<Price>(t));
        CHECK(sr->fundamental_value == Catch::Approx(10001.5));
        CHECK(sr->regime            == 1);
        CHECK(std::isnan(sr->news_cum_log));
    }
    CHECK(!r.next());
}

TEST_CASE("BinaryLog: metaorder round-trip", "[binlog]") {
    const std::string path = tmp_path("rt_meta.bin");
    {
        BinaryLogWriter w(path, 1u, 100u, 1u, "T");
        MetaorderEvent m;
        m.agent = 17; m.side = Side::Sell;
        m.tick_start = 100; m.tick_end = 195;
        m.qty_requested = 1500; m.qty_submitted = 1480; m.qty_executed = 1400;
        w.write_metaorder(m);
        MarketSnapshot s; s.tick = 200; s.mid_price = 9999;
        w.write_snapshot(s);
    }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());
    auto rec = r.next();
    REQUIRE(rec.has_value());
    auto* mr = std::get_if<MetaorderRecord>(&*rec);
    REQUIRE(mr != nullptr);
    CHECK(mr->agent_id      == 17);
    CHECK(mr->side          == 1);
    CHECK(mr->tick_start    == 100);
    CHECK(mr->tick_end      == 195);
    CHECK(mr->qty_requested == 1500);
    CHECK(mr->qty_submitted == 1480);
    CHECK(mr->qty_executed  == 1400);
    auto r2 = r.next(); REQUIRE(r2);
    CHECK(std::holds_alternative<MarketSnapshotRecord>(*r2));
    CHECK(!r.next());
}

// A log written by the v3 code (Etapa 0 fixture, seed 42, 3000 ticks) must
// still produce the very same facts table it produced then.
TEST_CASE("BinaryLog: v3 fixture reproduces its original facts table", "[binlog]") {
    const std::string dir = SIM_FIXTURES_DIR;
    LogData d = Report::extract(dir + "/log_v3_seed42_3000.bin");
    CHECK(d.n_snapshots == 3000);
    CHECK(d.n_trades    == 12586);
    CHECK(d.has_fundamental);
    CHECK(!d.has_news_cum);
    CHECK(d.news_cum.empty());
    CHECK(d.metaorders.empty());

    // The reference is the Etapa 0 text table; the layout has changed since,
    // so its rows are parsed and compared number by number.
    std::ifstream in(dir + "/log_v3_seed42_3000.report.txt");
    REQUIRE(in.good());
    struct Row { double value; bool pass; };
    std::vector<Row> ref;
    for (std::string ln; std::getline(in, ln);) {
        if (ln.rfind("| ", 0) != 0 || (ln.find("PASS") == std::string::npos && ln.find("FAIL") == std::string::npos))
            continue;
        // | idx | name | value | threshold | ci | PASS |  — the name may hold '|'.
        std::vector<std::string> cells;
        for (size_t p = 0, q; (q = ln.find('|', p + 1)) != std::string::npos; p = q)
            cells.push_back(ln.substr(p + 1, q - p - 1));
        REQUIRE(cells.size() >= 5);
        const size_t k = cells.size();
        ref.push_back({std::stod(cells[k - 4]), cells[k - 1].find("PASS") != std::string::npos});
    }
    REQUIRE(ref.size() == 8);

    const AnalysisReport now = Report::compute(d);
    REQUIRE(now.results.size() == 8);
    for (size_t i = 0; i < 8; ++i) {
        INFO("fact " << i + 1);
        char got[32];
        std::snprintf(got, sizeof got, "%.4f", now.results[i].value);
        CHECK(std::stod(got) == Catch::Approx(ref[i].value).margin(1e-9));
        CHECK(now.results[i].passed == ref[i].pass);
    }
}

TEST_CASE("BinaryLog: news event round-trip", "[binlog]") {
    const std::string path = tmp_path("rt_news.bin");
    {
        BinaryLogWriter w(path, 1u, 100u, 1u, "T");
        NewsEvent n;
        n.announce_tick     = 55;
        n.ticker            = "AAPL";
        n.impact_log_return = -0.025;
        n.duration_ticks    = 30.0;
        n.dispersion_sigma  = 0.4;
        w.write_news(n);
    }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());
    auto rec = r.next();
    REQUIRE(rec.has_value());
    auto* nr = std::get_if<NewsEventRecord>(&*rec);
    REQUIRE(nr != nullptr);
    CHECK(nr->announce_tick     == 55);
    CHECK(nr->impact_log_return == Catch::Approx(-0.025));
    CHECK(nr->duration_ticks    == Catch::Approx(30.0));
    CHECK(nr->dispersion_sigma  == Catch::Approx(0.4));
    CHECK(std::string(reinterpret_cast<const char*>(nr->ticker)) == "AAPL");
}

TEST_CASE("BinaryLog: regime change round-trip", "[binlog]") {
    const std::string path = tmp_path("rt_regime.bin");
    {
        BinaryLogWriter w(path, 1u, 100u, 1u, "T");
        w.write_regime_change(100, 0, 1);
    }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());
    auto rec = r.next();
    REQUIRE(rec.has_value());
    auto* rr = std::get_if<RegimeChangeRecord>(&*rec);
    REQUIRE(rr != nullptr);
    CHECK(rr->tick       == 100);
    CHECK(rr->old_regime == 0);
    CHECK(rr->new_regime == 1);
}

TEST_CASE("BinaryLog: mixed sequence preserves order", "[binlog]") {
    const std::string path = tmp_path("rt_mixed.bin");
    {
        BinaryLogWriter w(path, 1u, 200u, 1u, "MX");

        Trade t;  t.tick = 10; t.price = 1000; t.qty = 5;
        t.taker_side = Side::Buy;
        w.write_trade(t);

        MarketSnapshot s; s.tick = 10; s.mid_price = 1001;
        w.write_snapshot(s);

        w.write_regime_change(10, 0, 2);
    }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());

    auto r1 = r.next(); REQUIRE(r1); CHECK(std::holds_alternative<TradeRecord>(*r1));
    auto r2 = r.next(); REQUIRE(r2); CHECK(std::holds_alternative<MarketSnapshotRecord>(*r2));
    auto r3 = r.next(); REQUIRE(r3); CHECK(std::holds_alternative<RegimeChangeRecord>(*r3));
    CHECK(!r.next());   // EOF
}

TEST_CASE("BinaryLog: EOF on empty file after header", "[binlog]") {
    const std::string path = tmp_path("rt_empty.bin");
    { BinaryLogWriter w(path, 0u, 0u, 1u, "E"); }

    BinaryLogReader r(path);
    REQUIRE(r.read_header());
    CHECK(!r.next());
    CHECK(r.at_eof());
}
