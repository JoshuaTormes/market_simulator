#include <catch_amalgamated.hpp>
#include "clearing/Clearing.h"
#include "orderbook/FeeModel.h"

static const std::string TK = "TEST";

// ── Helpers ───────────────────────────────────────────────────────────────

static Trade make_trade(AgentId maker, AgentId taker, Side taker_side,
                         Price price, Qty qty,
                         Price fee_maker = 0, Price fee_taker = 0) {
    Trade t;
    t.maker_agent = maker;
    t.taker_agent = taker;
    t.taker_side  = taker_side;
    t.price       = price;
    t.qty         = qty;
    t.fee_maker   = fee_maker;
    t.fee_taker   = fee_taker;
    t.maker_id    = 0;
    t.taker_id    = 0;
    t.tick        = 1;
    t.seq_no      = 1;
    return t;
}

// ── Basic apply ───────────────────────────────────────────────────────────

TEST_CASE("clearing: taker buy / maker sell updates both sides", "[clearing]") {
    PositionLedger ledger;
    Clearing clearing(ledger, TK);

    // Maker was short (resting sell), taker buys
    Trade tr = make_trade(1, 2, Side::Buy, 100, 10);
    clearing.apply(tr);

    // Taker 2 bought 10 → long 10
    CHECK(ledger.net_qty(2, TK) == 10);
    // Maker 1 sold 10 → short 10
    CHECK(ledger.net_qty(1, TK) == -10);
}

TEST_CASE("clearing: taker sell / maker buy updates both sides", "[clearing]") {
    PositionLedger ledger;
    Clearing clearing(ledger, TK);

    Trade tr = make_trade(1, 2, Side::Sell, 100, 5);
    clearing.apply(tr);

    CHECK(ledger.net_qty(2, TK) == -5); // taker sold → short
    CHECK(ledger.net_qty(1, TK) ==  5); // maker bought → long
}

// ── Realized P&L returned ─────────────────────────────────────────────────

TEST_CASE("clearing: returns realized pnl for both sides", "[clearing]") {
    PositionLedger ledger;
    Clearing clearing(ledger, TK);

    // Agent 1 has a long position (seeded at 100)
    ledger.seed(1, TK, 10, 100);

    // Agent 1 is maker (resting bid), agent 2 is taker (sells to 1)
    // Wait: if taker_side = Sell, maker_side = Buy
    // Maker (agent 1) buys MORE at 110 — no realized yet (extending long)
    // Actually maker here is buying to add to their long
    // Let's test maker sell case: maker resting sell, taker buys
    // Maker (agent 1): was seeded long 10 @ 100, now sells 5 at 110
    Trade tr = make_trade(1, 2, Side::Buy, 110, 5);
    auto [maker_pnl, taker_pnl] = clearing.apply(tr);

    CHECK(maker_pnl == (110 - 100) * 5); // 50 profit for maker selling long
    CHECK(taker_pnl == 0);               // taker opened a new long (no realized yet)
}

// ── Fees ──────────────────────────────────────────────────────────────────

TEST_CASE("clearing: fees allocated to correct sides", "[clearing]") {
    PositionLedger ledger;
    Clearing clearing(ledger, TK);

    FeeModel fm;
    fm.taker_fee_per_lot   = 3;
    fm.maker_rebate_per_lot = 1;

    Price taker_fee  =  fm.taker_fee(10);   // +30
    Price maker_fee  = -fm.maker_rebate(10); // -10 (rebate credit)

    Trade tr = make_trade(1, 2, Side::Buy, 100, 10, maker_fee, taker_fee);
    clearing.apply(tr);

    CHECK(ledger.fees_net(2, TK) ==  30); // taker paid 30
    CHECK(ledger.fees_net(1, TK) == -10); // maker received 10 rebate
}

// ── Conservation after clearing ───────────────────────────────────────────

TEST_CASE("clearing: total_net_qty conserved after trades", "[clearing]") {
    PositionLedger ledger;
    Clearing clearing(ledger, TK);

    // Start: agent 1 has 20, agent 2 has 30
    ledger.seed(1, TK, 20, 100);
    ledger.seed(2, TK, 30, 100);
    CHECK(ledger.total_net_qty(TK) == 50);

    // Agent 1 (maker sell) → agent 3 (taker buy): 10 lots at 105
    Trade tr = make_trade(1, 3, Side::Buy, 105, 10);
    clearing.apply(tr);

    CHECK(ledger.net_qty(1, TK) == 10);  // sold 10 from long 20
    CHECK(ledger.net_qty(3, TK) == 10);  // new long
    CHECK(ledger.total_net_qty(TK) == 50); // conserved
}

// ── Multiple trades accumulate correctly ─────────────────────────────────

TEST_CASE("clearing: sequential trades accumulate pnl", "[clearing]") {
    PositionLedger ledger;
    Clearing clearing(ledger, TK);

    // Agent 1 goes long 10 at 100
    clearing.apply(make_trade(99, 1, Side::Buy, 100, 10));
    CHECK(ledger.net_qty(1, TK) == 10);

    // Agent 1 sells 5 at 110 (maker)
    clearing.apply(make_trade(1, 99, Side::Buy, 110, 5));
    CHECK(ledger.realized_pnl(1, TK) == 50);
    CHECK(ledger.net_qty(1, TK) == 5);

    // Agent 1 sells remaining 5 at 90 (loss)
    clearing.apply(make_trade(1, 99, Side::Buy, 90, 5));
    CHECK(ledger.realized_pnl(1, TK) == 50 + (-50)); // 0 net
    CHECK(ledger.net_qty(1, TK) == 0);
}
