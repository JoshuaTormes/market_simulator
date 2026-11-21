#include <catch_amalgamated.hpp>
#include "marketdata/SnapshotBuffer.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdint>

static MarketSnapshot make_snap(Tick tick, Price mid) {
    MarketSnapshot s;
    s.tick      = tick;
    s.mid_price = mid;
    s.spread    = 2;
    return s;
}

TEST_CASE("SnapshotBuffer: has_snapshot is false before first commit", "[snapbuf]") {
    SnapshotBuffer buf;
    CHECK(!buf.has_snapshot());
    MarketSnapshot out;
    CHECK(!buf.get_latest(out));
}

TEST_CASE("SnapshotBuffer: commit + get_latest round-trips tick and mid", "[snapbuf]") {
    SnapshotBuffer buf;
    buf.commit(make_snap(42, 10000));
    CHECK(buf.has_snapshot());

    MarketSnapshot out;
    REQUIRE(buf.get_latest(out));
    CHECK(out.tick      == 42);
    CHECK(out.mid_price == 10000);
}

TEST_CASE("SnapshotBuffer: always returns latest after multiple commits", "[snapbuf]") {
    SnapshotBuffer buf;
    for (Tick t = 0; t < 100; ++t)
        buf.commit(make_snap(t, 10000 + static_cast<Price>(t)));

    MarketSnapshot out;
    REQUIRE(buf.get_latest(out));
    CHECK(out.tick == 99);
    CHECK(out.mid_price == 10099);
}

TEST_CASE("SnapshotBuffer: checksum matches tick + mid_price", "[snapbuf]") {
    SnapshotBuffer buf;
    buf.commit(make_snap(77, 12345));

    MarketSnapshot out;
    uint64_t csum = 0;
    REQUIRE(buf.get_latest_with_checksum(out, csum));
    CHECK(csum == static_cast<uint64_t>(77) + static_cast<uint64_t>(12345));
}

TEST_CASE("SnapshotBuffer: concurrent writer and reader — no torn reads", "[snapbuf][threading]") {
    // 1 writer pumps 50,000 snapshots; 1 reader reads continuously.
    // Test: every read either returns false OR returns a consistent checksum.
    SnapshotBuffer buf;
    std::atomic<bool> stop{false};
    std::atomic<int>  torn_reads{0};
    std::atomic<int>  total_reads{0};

    // Writer thread
    std::thread writer([&] {
        for (Tick t = 1; t <= 50'000 && !stop.load(); ++t)
            buf.commit(make_snap(t, static_cast<Price>(10000 + t)));
    });

    // Reader thread: stops when writer signals done.
    std::thread reader([&] {
        MarketSnapshot out;
        uint64_t csum = 0;
        while (!stop.load(std::memory_order_acquire)) {
            if (buf.get_latest_with_checksum(out, csum)) {
                ++total_reads;
                uint64_t expected = static_cast<uint64_t>(out.tick)
                                  + static_cast<uint64_t>(out.mid_price);
                if (csum != expected) ++torn_reads;
            }
        }
    });

    writer.join();
    stop.store(true);
    reader.join();

    CHECK(torn_reads.load() == 0);
    CHECK(total_reads.load() > 0);
}

TEST_CASE("SnapshotBuffer: latest_index cycles through 3 slots", "[snapbuf]") {
    SnapshotBuffer buf;
    buf.commit(make_snap(1, 1000));
    int i0 = buf.latest_index();
    buf.commit(make_snap(2, 1001));
    int i1 = buf.latest_index();
    buf.commit(make_snap(3, 1002));
    int i2 = buf.latest_index();
    buf.commit(make_snap(4, 1003));
    int i3 = buf.latest_index();

    // All indices are valid slot numbers
    CHECK(i0 >= 0); CHECK(i0 < 3);
    CHECK(i1 >= 0); CHECK(i1 < 3);
    CHECK(i2 >= 0); CHECK(i2 < 3);
    CHECK(i3 >= 0); CHECK(i3 < 3);
    // No two consecutive writes share the same slot
    CHECK(i0 != i1);
    CHECK(i1 != i2);
    CHECK(i2 != i3);
}
