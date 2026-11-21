#include <catch_amalgamated.hpp>
#include "matching/MatchingEngine.h"
#include "core/RngService.h"
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────

static OrderId s_id = 1000;

static Order make_limit(Side side, Price price, Qty qty,
                         AgentId agent = 1) {
    Order o;
    o.id         = s_id++;
    o.agent_id   = agent;
    o.side       = side;
    o.type       = OrderType::Limit;
    o.price      = price;
    o.qty        = qty;
    o.submit_tick = 0;
    o.seq_no      = 0;
    return o;
}

static Order make_market(Side side, Qty qty, AgentId agent = 2) {
    Order o = make_limit(side, 0, qty, agent);
    o.type = OrderType::Market;
    return o;
}

static LatencyProfile zero_latency() {
    return {0, 0.0};
}

static LatencyProfile fixed_latency(Tick base) {
    return {base, 0.0};
}

// ── No latency: orders arrive immediately ─────────────────────────────────

TEST_CASE("zero latency: submit arrives at same tick", "[matching]") {
    MatchingEngine eng("TEST");

    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    eng.submit(make_limit(Side::Buy,  100, 10, 1), 5, zero_latency());
    eng.submit(make_limit(Side::Sell, 100, 10, 2), 5, zero_latency());

    eng.process_until(5, cb);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].qty == 10);
}

TEST_CASE("zero latency: process_until at earlier tick sees no events", "[matching]") {
    MatchingEngine eng("TEST");

    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    eng.submit(make_limit(Side::Buy,  100, 10, 1), 5, zero_latency());
    eng.submit(make_limit(Side::Sell, 100, 10, 2), 5, zero_latency());

    // Drain at tick 4 — nothing arrives yet (arrival_tick = 5)
    eng.process_until(4, cb);
    CHECK(trades.empty());

    // Now drain at tick 5 — both events land and match
    eng.process_until(5, cb);
    CHECK(trades.size() == 1);
}

// ── Base latency: orders arrive after delay ───────────────────────────────

TEST_CASE("base latency: order not visible before arrival_tick", "[matching]") {
    MatchingEngine eng("TEST");

    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    // Submit at tick 0 with 5-tick latency → arrival_tick = 5
    eng.submit(make_limit(Side::Buy,  100, 5, 1), 0, fixed_latency(5));
    eng.submit(make_limit(Side::Sell, 100, 5, 2), 0, fixed_latency(5));

    // Tick 4: nothing yet
    eng.process_until(4, cb);
    CHECK(trades.empty());

    // Tick 5: both land and match
    eng.process_until(5, cb);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].qty == 5);
}

TEST_CASE("base latency: orders at different delays arrive in order", "[matching]") {
    MatchingEngine eng("TEST");

    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    // Ask arrives at tick 3, bid arrives at tick 5 → match at tick 5
    eng.submit(make_limit(Side::Sell, 100, 5, 1), 0, fixed_latency(3));
    eng.submit(make_limit(Side::Buy,  100, 5, 2), 0, fixed_latency(5));

    eng.process_until(3, cb); // ask lands, rests in book, no match
    CHECK(trades.empty());

    eng.process_until(5, cb); // bid lands, book.match() fires
    REQUIRE(trades.size() == 1);
}

// ── Cancel via MatchingEngine ─────────────────────────────────────────────

TEST_CASE("cancel: order removed before it would match", "[matching]") {
    MatchingEngine eng("TEST");

    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    Order bid = make_limit(Side::Buy, 100, 5, 1);
    eng.submit(bid, 0, zero_latency());
    eng.process_until(0, cb); // bid rests

    // Cancel the bid
    eng.cancel(bid.id, bid.agent_id, 0, zero_latency());

    // Submit crossing ask
    eng.submit(make_limit(Side::Sell, 100, 5, 2), 0, zero_latency());
    eng.process_until(0, cb);

    // No trade — bid was cancelled before ask arrived
    CHECK(trades.empty());
}

// ── Market order routed through MatchingEngine ────────────────────────────

TEST_CASE("market order fills against resting limit", "[matching]") {
    MatchingEngine eng("TEST");

    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    eng.submit(make_limit(Side::Sell, 101, 10, 1), 0, zero_latency());
    eng.process_until(0, cb);

    eng.submit(make_market(Side::Buy, 7, 2), 0, zero_latency());
    eng.process_until(0, cb);

    REQUIRE(trades.size() == 1);
    CHECK(trades[0].qty == 7);
}

// ── Determinism: same seed → same sequence of trades ─────────────────────

TEST_CASE("determinism: same seed produces identical trade sequence", "[matching]") {
    RngService rng_a(1234);
    RngService rng_b(1234);

    auto run_sim = [&](RngService& rng) {
        MatchingEngine eng("TEST");
        eng.set_rng(&rng);

        LatencyProfile lp{2, 0.5}; // base + jitter

        std::vector<Trade> trades;
        auto cb = [&](const Trade& t) { trades.push_back(t); };

        for (int i = 0; i < 20; ++i) {
            Order o = make_limit(Side::Buy,  100, 1, 1);
            o.seq_no = static_cast<SeqNo>(i);
            eng.submit(o, static_cast<Tick>(i), lp);

            Order s = make_limit(Side::Sell, 100, 1, 2);
            s.seq_no = static_cast<SeqNo>(100 + i);
            eng.submit(s, static_cast<Tick>(i), lp);

            eng.process_until(static_cast<Tick>(i + 5), cb);
        }
        return trades;
    };

    auto ta = run_sim(rng_a);
    auto tb = run_sim(rng_b);

    REQUIRE(ta.size() == tb.size());
    for (size_t i = 0; i < ta.size(); ++i) {
        CHECK(ta[i].tick  == tb[i].tick);
        CHECK(ta[i].price == tb[i].price);
        CHECK(ta[i].qty   == tb[i].qty);
    }
}

// ── Seq ordering: process_until applies events in (arrival_tick, seq_no) order ──

TEST_CASE("seq ordering: earlier seq_no processed first at same tick", "[matching]") {
    MatchingEngine eng("TEST");

    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    // Submit ask first (gets lower seq_no), then bid — both arrive at tick 0
    eng.submit(make_limit(Side::Sell, 100, 5, 2), 0, zero_latency());
    eng.submit(make_limit(Side::Buy,  100, 5, 1), 0, zero_latency());

    eng.process_until(0, cb);
    // Ask rested first (lower seq_no → arrived first) → ask is maker
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].maker_agent == 2);
    CHECK(trades[0].taker_agent == 1);
}

// ── Expire via MatchingEngine ─────────────────────────────────────────────

TEST_CASE("expire: TTL orders removed by MatchingEngine::expire", "[matching]") {
    MatchingEngine eng("TEST");

    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    Order o = make_limit(Side::Buy, 100, 5, 1);
    o.ttl_expiry = 3;
    eng.submit(o, 0, zero_latency());
    eng.process_until(0, cb);

    eng.expire(3);

    // Submit a crossing sell — no trade because bid expired
    eng.submit(make_limit(Side::Sell, 100, 5, 2), 3, zero_latency());
    eng.process_until(3, cb);

    CHECK(trades.empty());
}
