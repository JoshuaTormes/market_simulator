#include <catch_amalgamated.hpp>
#include "matching/MatchingEngine.h"
#include "orderbook/OrderBookV2.h"
#include <vector>

// A requoting market maker cancels its whole book every tick.  These tests pin
// the two properties that matter: the mass cancel touches only the calling
// agent, and it is ordered before the quotes the same agent sends after it.

static OrderId s_id = 1;

static Order make_limit(Side side, Price price, Qty qty, AgentId agent) {
    Order o;
    o.id          = s_id++;
    o.agent_id    = agent;
    o.side        = side;
    o.type        = OrderType::Limit;
    o.price       = price;
    o.qty         = qty;
    o.submit_tick = 0;
    o.seq_no      = 0;
    return o;
}

static LatencyProfile zero_latency() { return {0, 0.0}; }

TEST_CASE("OrderBookV2: cancel_all removes only the owner's orders", "[cancel_all]") {
    OrderBookV2 book;

    book.add_limit(make_limit(Side::Buy,   99, 10, 7));
    book.add_limit(make_limit(Side::Buy,   98, 20, 7));
    book.add_limit(make_limit(Side::Sell, 101, 30, 7));
    book.add_limit(make_limit(Side::Sell, 102, 40, 9));

    CHECK(book.order_count()    == 4);
    CHECK(book.order_count(7)   == 3);

    CHECK(book.cancel_all(7)    == 3);
    CHECK(book.order_count(7)   == 0);
    CHECK(book.order_count()    == 1);
    CHECK(book.empty_bids());
    CHECK(book.best_ask()       == 102);   // agent 9 untouched

    // Idempotent: nothing left to cancel.
    CHECK(book.cancel_all(7)    == 0);
}

TEST_CASE("OrderBookV2: owner index survives fills and expiry", "[cancel_all]") {
    OrderBookV2 book;

    // A resting bid from agent 1 fully filled by a market sell from agent 2
    // must leave no owner entry behind, or cancel_all would chase a dead id.
    book.add_limit(make_limit(Side::Buy, 100, 10, 1));
    Order taker = make_limit(Side::Sell, 0, 10, 2);
    taker.type  = OrderType::Market;
    auto trades = book.execute_aggressive(taker, 1);
    REQUIRE(trades.size() == 1);
    CHECK(book.order_count(1) == 0);
    CHECK(book.cancel_all(1)  == 0);

    // Same for a TTL expiry.
    Order gtt      = make_limit(Side::Buy, 95, 10, 1);
    gtt.ttl_expiry = 5;
    book.add_limit(gtt);
    CHECK(book.order_count(1) == 1);
    book.expire(5);
    CHECK(book.order_count(1) == 0);
    CHECK(book.cancel_all(1)  == 0);
}

TEST_CASE("MatchingEngine: CancelAll then Submit leaves only the new order",
          "[cancel_all]") {
    MatchingEngine eng("TEST");
    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    // Tick 0: three quotes rest.
    eng.submit(make_limit(Side::Buy,   99, 10, 7), 0, zero_latency());
    eng.submit(make_limit(Side::Buy,   98, 10, 7), 0, zero_latency());
    eng.submit(make_limit(Side::Sell, 101, 10, 7), 0, zero_latency());
    eng.process_until(0, cb);
    REQUIRE(eng.book().order_count(7) == 3);

    // Tick 1: requote — mass cancel first, then a single fresh bid.  Both
    // arrive on the same tick, so only the seq ordering keeps the cancel from
    // wiping the new quote too.
    eng.cancel_all(7, 1, zero_latency());
    eng.submit(make_limit(Side::Buy, 100, 25, 7), 1, zero_latency());
    eng.process_until(1, cb);

    CHECK(eng.book().order_count(7) == 1);
    CHECK(eng.book().best_bid()     == 100);
    CHECK(eng.book().total_bid_depth() == 25);
    CHECK(eng.book().empty_asks());
    CHECK(trades.empty());
}

TEST_CASE("MatchingEngine: CancelAll obeys agent latency", "[cancel_all]") {
    MatchingEngine eng("TEST");
    std::vector<Trade> trades;
    auto cb = [&](const Trade& t) { trades.push_back(t); };

    eng.submit(make_limit(Side::Buy, 99, 10, 7), 0, zero_latency());
    eng.process_until(0, cb);
    REQUIRE(eng.book().order_count(7) == 1);

    eng.cancel_all(7, 0, LatencyProfile{3, 0.0});
    eng.process_until(2, cb);
    CHECK(eng.book().order_count(7) == 1);   // still in flight
    eng.process_until(3, cb);
    CHECK(eng.book().order_count(7) == 0);
}
