// Round-trip test: write a set of typed records, read them back,
// verify every field survives serialisation + deserialisation unchanged.
#include <catch_amalgamated.hpp>
#include "persistence/BinaryLogWriter.h"
#include "persistence/BinaryLogReader.h"
#include "persistence/EventSchema.h"
#include "orderbook/Trade.h"
#include "marketdata/MarketSnapshot.h"
#include "economics/NewsEvent.h"
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
    CHECK(sr->regime            == 1);
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
