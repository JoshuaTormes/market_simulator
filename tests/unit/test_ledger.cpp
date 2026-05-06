#include <catch_amalgamated.hpp>
#include "ledger/PositionLedger.h"

static const std::string TK = "AAPL";

// ── Seed ──────────────────────────────────────────────────────────────────

TEST_CASE("seed: sets initial long position", "[ledger]") {
    PositionLedger ledger;
    ledger.seed(1, TK, 100, 50);
    CHECK(ledger.net_qty(1, TK) == 100);
    CHECK(ledger.avg_cost(1, TK) == 50);
}

TEST_CASE("seed: multiple agents, conservation holds", "[ledger]") {
    PositionLedger ledger;
    ledger.seed(1, TK, 60, 100);
    ledger.seed(2, TK, 40, 100);
    CHECK(ledger.total_net_qty(TK) == 100);
}

// ── Long position management ───────────────────────────────────────────────

TEST_CASE("buy: opens long from flat", "[ledger][long]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Buy, TK, 10, 100, 0);
    CHECK(ledger.net_qty(1, TK) == 10);
    CHECK(ledger.avg_cost(1, TK) == 100);
}

TEST_CASE("buy: accumulates avg cost correctly", "[ledger][long]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Buy, TK, 10, 100, 0); // cost_basis = 1000
    ledger.apply(1, Side::Buy, TK, 10, 120, 0); // cost_basis = 2200
    CHECK(ledger.net_qty(1, TK) == 20);
    CHECK(ledger.avg_cost(1, TK) == 110); // 2200 / 20
}

TEST_CASE("sell: reduces long and realizes pnl", "[ledger][long]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Buy, TK, 10, 100, 0); // avg cost = 100
    Price pnl = ledger.apply(1, Side::Sell, TK, 5, 120, 0); // sell 5 at 120

    CHECK(pnl == (120 - 100) * 5);         // 100 profit
    CHECK(ledger.net_qty(1, TK) == 5);
    CHECK(ledger.realized_pnl(1, TK) == 100);
}

TEST_CASE("sell: close entire long — flat position", "[ledger][long]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Buy, TK, 10, 100, 0);
    ledger.apply(1, Side::Sell, TK, 10, 110, 0);

    CHECK(ledger.net_qty(1, TK) == 0);
    CHECK(ledger.realized_pnl(1, TK) == 100); // 10 * 10
    CHECK(ledger.avg_cost(1, TK) == 0);       // flat
}

TEST_CASE("sell beyond long: flips to short", "[ledger][long]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Buy, TK, 5, 100, 0);  // long 5 at avg 100
    ledger.apply(1, Side::Sell, TK, 8, 110, 0); // sell 8: close 5 long + short 3

    CHECK(ledger.net_qty(1, TK) == -3);
    CHECK(ledger.realized_pnl(1, TK) == (110 - 100) * 5); // only long closure realizes
    CHECK(ledger.avg_cost(1, TK) == 110); // short of 3 opened at 110
}

// ── Short position management ─────────────────────────────────────────────

TEST_CASE("sell: opens short from flat", "[ledger][short]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Sell, TK, 10, 100, 0);
    CHECK(ledger.net_qty(1, TK) == -10);
    CHECK(ledger.avg_cost(1, TK) == 100);
}

TEST_CASE("buy: covers short and realizes pnl", "[ledger][short]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Sell, TK, 10, 100, 0); // short 10 at 100
    Price pnl = ledger.apply(1, Side::Buy, TK, 10, 90, 0);  // cover at 90

    CHECK(pnl == (100 - 90) * 10);               // 100 profit on short
    CHECK(ledger.net_qty(1, TK) == 0);
    CHECK(ledger.realized_pnl(1, TK) == 100);
}

TEST_CASE("buy to cover short + flip to long", "[ledger][short]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Sell, TK, 5, 100, 0);  // short 5 at 100
    ledger.apply(1, Side::Buy, TK, 8, 90, 0);    // cover 5 + long 3

    CHECK(ledger.net_qty(1, TK) == 3);
    CHECK(ledger.realized_pnl(1, TK) == (100 - 90) * 5); // 50
    CHECK(ledger.avg_cost(1, TK) == 90); // long of 3 opened at 90
}

TEST_CASE("short: add to existing short position", "[ledger][short]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Sell, TK, 5, 100, 0);
    ledger.apply(1, Side::Sell, TK, 5, 110, 0);
    CHECK(ledger.net_qty(1, TK) == -10);
    CHECK(ledger.avg_cost(1, TK) == 105); // (500 + 550) / 10
}

// ── Unrealized P&L ────────────────────────────────────────────────────────

TEST_CASE("unrealized_pnl: long position", "[ledger]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Buy, TK, 10, 100, 0); // long 10 at 100
    CHECK(ledger.unrealized_pnl(1, TK, 110) == 100); // 10 * (110 - 100)
    CHECK(ledger.unrealized_pnl(1, TK, 90)  == -100);
}

TEST_CASE("unrealized_pnl: short position", "[ledger]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Sell, TK, 10, 100, 0); // short 10 at 100
    CHECK(ledger.unrealized_pnl(1, TK, 90)  == 100);  // price fell: profit
    CHECK(ledger.unrealized_pnl(1, TK, 110) == -100); // price rose: loss
}

TEST_CASE("unrealized_pnl: flat position returns 0", "[ledger]") {
    PositionLedger ledger;
    CHECK(ledger.unrealized_pnl(1, TK, 100) == 0);
}

// ── Fees ──────────────────────────────────────────────────────────────────

TEST_CASE("fees: accumulate per agent", "[ledger][fees]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Buy,  TK, 10, 100, 5);  // taker fee = 5
    ledger.apply(1, Side::Sell, TK, 10, 110, -2); // maker rebate = -2
    CHECK(ledger.fees_net(1, TK) == 3); // 5 + (-2) = 3 net fees
}

// ── Conservation invariant ─────────────────────────────────────────────────

TEST_CASE("conservation: trades are zero-sum", "[ledger][invariant]") {
    PositionLedger ledger;
    // Seed 100 shares across two agents
    ledger.seed(1, TK, 60, 100);
    ledger.seed(2, TK, 40, 100);
    CHECK(ledger.total_net_qty(TK) == 100);

    // Agent 1 sells 20 to agent 2: still sums to 100
    ledger.apply(1, Side::Sell, TK, 20, 105, 0);
    ledger.apply(2, Side::Buy,  TK, 20, 105, 0);
    CHECK(ledger.total_net_qty(TK) == 100);

    // Agent 3 goes short 10 (net change = -10 + 10 from some buyer)
    ledger.apply(3, Side::Sell, TK, 10, 105, 0);
    ledger.apply(1, Side::Buy,  TK, 10, 105, 0);
    CHECK(ledger.total_net_qty(TK) == 100); // shorts cancel longs
}

// ── Multiple tickers ──────────────────────────────────────────────────────

TEST_CASE("multiple tickers are independent", "[ledger]") {
    PositionLedger ledger;
    ledger.apply(1, Side::Buy, "AAPL", 10, 150, 0);
    ledger.apply(1, Side::Buy, "MSFT", 5,  300, 0);

    CHECK(ledger.net_qty(1, "AAPL") == 10);
    CHECK(ledger.net_qty(1, "MSFT") == 5);
    CHECK(ledger.avg_cost(1, "AAPL") == 150);
    CHECK(ledger.avg_cost(1, "MSFT") == 300);
}
