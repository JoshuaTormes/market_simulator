#include <catch_amalgamated.hpp>
#include "orderbook/OrderBookV2.h"
#include "core/RngService.h"
#include <random>
#include <algorithm>

// ── Helpers ───────────────────────────────────────────────────────────────

static OrderId next_id() {
    static OrderId counter = 1;
    return counter++;
}

static Order make_limit(Side side, Price price, Qty qty,
                         AgentId agent = 1, OrderId id = 0) {
    Order o;
    o.id         = id ? id : next_id();
    o.agent_id   = agent;
    o.side       = side;
    o.type       = OrderType::Limit;
    o.price      = price;
    o.qty        = qty;
    o.submit_tick= 0;
    o.seq_no     = 0;
    return o;
}

static Order make_market(Side side, Qty qty, AgentId agent = 2) {
    Order o = make_limit(side, 0, qty, agent);
    o.type = OrderType::Market;
    return o;
}

static Order make_ioc(Side side, Price price, Qty qty, AgentId agent = 2) {
    Order o = make_limit(side, price, qty, agent);
    o.type = OrderType::IOC;
    return o;
}

static Order make_fok(Side side, Price price, Qty qty, AgentId agent = 2) {
    Order o = make_limit(side, price, qty, agent);
    o.type = OrderType::FOK;
    return o;
}

static Order make_post_only(Side side, Price price, Qty qty, AgentId agent = 1) {
    Order o = make_limit(side, price, qty, agent);
    o.type = OrderType::PostOnly;
    return o;
}

// ── Basic queries ─────────────────────────────────────────────────────────

TEST_CASE("empty book has no best bid/ask", "[orderbook]") {
    OrderBookV2 book;
    CHECK(book.best_bid() == 0);
    CHECK(book.best_ask() == 0);
    CHECK(book.order_count() == 0);
}

TEST_CASE("add single bid and ask", "[orderbook]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Buy,  100, 10));
    book.add_limit(make_limit(Side::Sell, 101, 5));
    CHECK(book.best_bid() == 100);
    CHECK(book.best_ask() == 101);
    CHECK(book.order_count() == 2);
}

TEST_CASE("multiple bids: best_bid = highest price", "[orderbook]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Buy, 98, 10));
    book.add_limit(make_limit(Side::Buy, 100, 5));
    book.add_limit(make_limit(Side::Buy, 99, 7));
    CHECK(book.best_bid() == 100);
}

TEST_CASE("multiple asks: best_ask = lowest price", "[orderbook]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Sell, 103, 5));
    book.add_limit(make_limit(Side::Sell, 101, 3));
    book.add_limit(make_limit(Side::Sell, 102, 8));
    CHECK(book.best_ask() == 101);
}

// ── add_limit / PostOnly ──────────────────────────────────────────────────

TEST_CASE("PostOnly rejected if it would cross", "[orderbook]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Sell, 100, 10));

    // Buy PostOnly at 100 would cross (bid >= best ask) → rejected
    auto r = book.add_limit(make_post_only(Side::Buy, 100, 5));
    CHECK(!r.has_value());
    CHECK(book.order_count() == 1); // only the ask remains
}

TEST_CASE("PostOnly accepted if it does not cross", "[orderbook]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Sell, 101, 10));

    auto r = book.add_limit(make_post_only(Side::Buy, 100, 5));
    CHECK(r.has_value());
    CHECK(book.order_count() == 2);
}

TEST_CASE("PostOnly sell rejected if it crosses", "[orderbook]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Buy, 100, 10));

    // Sell PostOnly at 100 (bid >= ask, so it crosses)
    auto r = book.add_limit(make_post_only(Side::Sell, 100, 5));
    CHECK(!r.has_value());
}

// ── Cancel ────────────────────────────────────────────────────────────────

TEST_CASE("cancel removes order", "[orderbook]") {
    OrderBookV2 book;
    auto o = make_limit(Side::Buy, 100, 10);
    OrderId id = o.id;
    book.add_limit(o);
    CHECK(book.order_count() == 1);

    bool ok = book.cancel(id);
    CHECK(ok);
    CHECK(book.order_count() == 0);
    CHECK(book.best_bid() == 0);
}

TEST_CASE("cancel of nonexistent order returns false", "[orderbook]") {
    OrderBookV2 book;
    CHECK_FALSE(book.cancel(9999));
}

TEST_CASE("cancel prunes empty price level", "[orderbook]") {
    OrderBookV2 book;
    auto o1 = make_limit(Side::Buy, 100, 5);
    auto o2 = make_limit(Side::Buy, 100, 3);
    book.add_limit(o1);
    book.add_limit(o2);
    book.cancel(o1.id);
    book.cancel(o2.id);
    CHECK(book.best_bid() == 0); // level pruned
}

// ── match (resting-vs-resting) ────────────────────────────────────────────

TEST_CASE("match produces trade when bid >= ask", "[orderbook]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Buy,  100, 10, 1));
    book.add_limit(make_limit(Side::Sell, 100, 10, 2));

    auto trades = book.match(1);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].qty == 10);
    CHECK(trades[0].price == 100);
}

TEST_CASE("match: no trade when no cross", "[orderbook]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Buy,  99, 10));
    book.add_limit(make_limit(Side::Sell, 101, 5));

    auto trades = book.match(1);
    CHECK(trades.empty());
    CHECK(book.order_count() == 2);
}

TEST_CASE("match: partial fill preserves remainder", "[orderbook]") {
    OrderBookV2 book;
    // bid 15, ask 10 → trade of 10, bid remainder = 5
    Order bid = make_limit(Side::Buy,  100, 15, 1);
    Order ask = make_limit(Side::Sell, 100, 10, 2);
    bid.seq_no = 1; ask.seq_no = 2;
    book.add_limit(bid);
    book.add_limit(ask);

    auto trades = book.match(1);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].qty == 10);

    // ask fully filled; bid has 5 left
    CHECK(book.empty_asks());
    CHECK(book.total_bid_depth() == 5);
}

TEST_CASE("match: price-time priority — older order is maker", "[orderbook]") {
    OrderBookV2 book;
    Order bid  = make_limit(Side::Buy,  100, 5, 1); bid.seq_no  = 1;
    Order ask  = make_limit(Side::Sell, 100, 5, 2); ask.seq_no  = 2;
    book.add_limit(bid);
    book.add_limit(ask);

    auto trades = book.match(1);
    REQUIRE(!trades.empty());
    // bid arrived first (lower seq_no) → bid is maker
    CHECK(trades[0].maker_agent == 1);
    CHECK(trades[0].taker_agent == 2);
    CHECK(trades[0].taker_side  == Side::Sell);
}

TEST_CASE("match: STP CancelBoth removes both self-trade orders", "[orderbook]") {
    OrderBookV2 book;
    Order bid = make_limit(Side::Buy,  100, 5, 1); bid.seq_no = 1;
    Order ask = make_limit(Side::Sell, 100, 5, 1); ask.seq_no = 2; // same agent!
    book.add_limit(bid);
    book.add_limit(ask);

    auto trades = book.match(1);
    CHECK(trades.empty());        // no trade — both cancelled
    CHECK(book.order_count() == 0);
}

TEST_CASE("match: multiple levels cleared in order", "[orderbook]") {
    OrderBookV2 book;
    // Two overlapping ask levels vs one large bid
    Order a1 = make_limit(Side::Sell, 99, 5, 2); a1.seq_no = 1;
    Order a2 = make_limit(Side::Sell, 100, 5, 3); a2.seq_no = 2;
    Order bid = make_limit(Side::Buy, 101, 10, 1); bid.seq_no = 3;
    book.add_limit(a1);
    book.add_limit(a2);
    book.add_limit(bid);

    // bid crosses both ask levels
    auto trades = book.match(1);
    CHECK(trades.size() == 2);
    CHECK(book.order_count() == 0);
}

// ── execute_aggressive (Market / IOC / FOK) ───────────────────────────────

TEST_CASE("market buy sweeps asks", "[orderbook][aggressive]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Sell, 101, 5, 1));
    book.add_limit(make_limit(Side::Sell, 102, 5, 1));

    auto trades = book.execute_aggressive(make_market(Side::Buy, 8), 1);
    REQUIRE(trades.size() == 2);
    CHECK(trades[0].price == 101);
    CHECK(trades[0].qty   == 5);
    CHECK(trades[1].price == 102);
    CHECK(trades[1].qty   == 3);
    CHECK(book.total_ask_depth() == 2); // 5 - 3 = 2 remaining at 102
}

TEST_CASE("market sell sweeps bids", "[orderbook][aggressive]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Buy, 100, 5, 1));
    book.add_limit(make_limit(Side::Buy, 99,  5, 1));

    auto trades = book.execute_aggressive(make_market(Side::Sell, 7), 1);
    CHECK(trades.size() == 2);
    CHECK(book.total_bid_depth() == 3); // 10 - 7 = 3 remaining
}

TEST_CASE("IOC fills partial, drops unfilled residual", "[orderbook][aggressive]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Sell, 101, 3, 1));

    // IOC for 10 but only 3 available
    auto trades = book.execute_aggressive(make_ioc(Side::Buy, 101, 10), 1);
    CHECK(trades.size() == 1);
    CHECK(trades[0].qty == 3);
    CHECK(book.empty_asks()); // maker fully consumed
}

TEST_CASE("FOK succeeds when full qty available", "[orderbook][aggressive]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Sell, 101, 10, 1));

    auto trades = book.execute_aggressive(make_fok(Side::Buy, 101, 10), 1);
    REQUIRE(trades.size() == 1);
    CHECK(trades[0].qty == 10);
    CHECK(book.empty_asks());
}

TEST_CASE("FOK rejected when insufficient qty — book untouched", "[orderbook][aggressive]") {
    OrderBookV2 book;
    book.add_limit(make_limit(Side::Sell, 101, 5, 1));

    auto trades = book.execute_aggressive(make_fok(Side::Buy, 101, 10), 1);
    CHECK(trades.empty());
    CHECK(book.total_ask_depth() == 5); // book untouched
}

TEST_CASE("aggressive STP: skip maker with same agent_id", "[orderbook][aggressive]") {
    OrderBookV2 book;
    Order maker = make_limit(Side::Sell, 100, 5, 42);
    book.add_limit(maker);

    // Market buy from same agent — should skip maker (STP) and consume nothing
    Order taker = make_market(Side::Buy, 5, 42);
    auto trades = book.execute_aggressive(taker, 1);
    CHECK(trades.empty());
}

// ── Modify ────────────────────────────────────────────────────────────────

TEST_CASE("modify: reduce qty preserves position", "[orderbook][modify]") {
    OrderBookV2 book;
    Order o1 = make_limit(Side::Buy, 100, 10, 1); o1.seq_no = 1;
    Order o2 = make_limit(Side::Buy, 100, 10, 2); o2.seq_no = 2;
    book.add_limit(o1);
    book.add_limit(o2);

    // Reduce o1's qty — should stay at front of level
    book.modify(o1.id, 100, 5);

    auto pos = book.queue_position(o1.id);
    REQUIRE(pos.has_value());
    CHECK(*pos == 0); // still at position 0 (front)
}

TEST_CASE("modify: increase qty loses position (re-insert at tail)", "[orderbook][modify]") {
    OrderBookV2 book;
    Order o1 = make_limit(Side::Buy, 100, 5, 1); o1.seq_no = 1;
    Order o2 = make_limit(Side::Buy, 100, 5, 2); o2.seq_no = 2;
    book.add_limit(o1);
    book.add_limit(o2);

    book.modify(o1.id, 100, 10); // increase → re-insert at tail

    auto pos = book.queue_position(o1.id);
    REQUIRE(pos.has_value());
    CHECK(*pos == 1); // now behind o2
}

TEST_CASE("modify: price change re-inserts at new level", "[orderbook][modify]") {
    OrderBookV2 book;
    Order o = make_limit(Side::Buy, 100, 5, 1); o.seq_no = 1;
    book.add_limit(o);

    book.modify(o.id, 99, 5); // move to new price level

    CHECK(book.best_bid() == 99);
    CHECK(book.order_count() == 1);
}

TEST_CASE("modify: nonexistent order returns false", "[orderbook][modify]") {
    OrderBookV2 book;
    CHECK_FALSE(book.modify(9999, 100, 5));
}

// ── Expire ────────────────────────────────────────────────────────────────

TEST_CASE("expire: removes orders past ttl", "[orderbook]") {
    OrderBookV2 book;
    Order o = make_limit(Side::Buy, 100, 5);
    o.ttl_expiry = 10; // expires at tick 10
    book.add_limit(o);

    book.expire(9);  // tick 9 — not yet expired
    CHECK(book.order_count() == 1);

    book.expire(10); // tick 10 — expired
    CHECK(book.order_count() == 0);
}

TEST_CASE("expire: ignores orders with ttl = 0 (no expiry)", "[orderbook]") {
    OrderBookV2 book;
    Order o = make_limit(Side::Buy, 100, 5);
    o.ttl_expiry = 0;
    book.add_limit(o);

    book.expire(9999);
    CHECK(book.order_count() == 1);
}

// ── Depth queries ─────────────────────────────────────────────────────────

TEST_CASE("top_bids / top_asks aggregation", "[orderbook]") {
    OrderBookV2 book;
    // Two orders at same bid level
    book.add_limit(make_limit(Side::Buy, 100, 5, 1));
    book.add_limit(make_limit(Side::Buy, 100, 3, 2));
    book.add_limit(make_limit(Side::Buy, 99,  7, 1));

    auto bids = book.top_bids(2);
    REQUIRE(bids.size() == 2);
    CHECK(bids[0].price == 100);
    CHECK(bids[0].qty   == 8);  // 5+3
    CHECK(bids[0].order_count == 2);
    CHECK(bids[1].price == 99);
    CHECK(bids[1].qty   == 7);
}

// ── Invariant property test ───────────────────────────────────────────────

TEST_CASE("property: best_bid < best_ask always holds after random ops", "[orderbook][property]") {
    RngService rng(42);
    auto eng = rng.for_consumer("prop_test");

    std::uniform_int_distribution<int>    op_dist(0, 3);       // 0=bid,1=ask,2=cancel_bid,3=cancel_ask
    std::uniform_int_distribution<Price>  price_dist(95, 105);
    std::uniform_int_distribution<Qty>    qty_dist(1, 20);
    std::uniform_int_distribution<AgentId> agent_dist(1, 5);

    OrderBookV2 book;
    std::vector<OrderId> bid_ids, ask_ids;

    for (int i = 0; i < 10000; ++i) {
        int op = op_dist(eng);

        if (op == 0) {
            Order o = make_limit(Side::Buy, price_dist(eng), qty_dist(eng), agent_dist(eng));
            book.add_limit(o);
            bid_ids.push_back(o.id);
        } else if (op == 1) {
            Order o = make_limit(Side::Sell, price_dist(eng), qty_dist(eng), agent_dist(eng));
            book.add_limit(o);
            ask_ids.push_back(o.id);
        } else if (op == 2 && !bid_ids.empty()) {
            std::uniform_int_distribution<size_t> idx(0, bid_ids.size() - 1);
            book.cancel(bid_ids[idx(eng)]);
        } else if (op == 3 && !ask_ids.empty()) {
            std::uniform_int_distribution<size_t> idx(0, ask_ids.size() - 1);
            book.cancel(ask_ids[idx(eng)]);
        }

        // Match every few ops and check invariant immediately after
        if (i % 50 == 0) {
            book.match(static_cast<Tick>(i));
            // Invariant: after matching, no crossing can remain
            if (!book.empty_bids() && !book.empty_asks()) {
                CHECK(book.best_bid() < book.best_ask());
            }
        }
    }
}
