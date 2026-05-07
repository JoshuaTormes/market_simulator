#include <catch_amalgamated.hpp>
#include "risk/RiskGate.h"
#include "agents/AgentRunner.h"
#include "agents/NoiseTrader.h"
#include "ledger/PositionLedger.h"
#include "core/RngService.h"

// Helper: build an AgentAction with a SubmitOrder
static AgentAction make_aa(IAgent* agent, Side side, Qty qty, Price price) {
    return { agent, SubmitOrder{ side, OrderType::Limit, price, qty, "T" } };
}

// Minimal concrete agent for testing
class TestAgent : public NoiseTrader {
public:
    TestAgent(AgentId id, RiskLimits rl, RngService& rng)
        : NoiseTrader(id, "T", rng.for_consumer("ta_" + std::to_string(id)),
                      NoiseTrader::Params{}, rl) {}
};

TEST_CASE("RiskGate: order within limits is approved", "[riskgate]") {
    PositionLedger ledger;
    RiskGate gate(ledger, "T");
    RngService rng(42);

    RiskLimits rl;
    rl.max_position = 100;
    rl.max_notional = 10'000'000LL;

    TestAgent agent(1, rl, rng);
    auto aa = make_aa(&agent, Side::Buy, 10, 10000);
    auto result = gate.filter({ aa }, 10000);

    REQUIRE(result.size() == 1);
    CHECK(result[0].approved);
    CHECK(!result[0].margin_call);
}

TEST_CASE("RiskGate: order exceeding max_position is rejected", "[riskgate]") {
    PositionLedger ledger;
    RiskGate gate(ledger, "T");
    RngService rng(42);

    RiskLimits rl;
    rl.max_position = 10;

    TestAgent agent(1, rl, rng);
    // Current position = 0, order qty = 15 → projected = 15 > 10
    auto aa = make_aa(&agent, Side::Buy, 15, 10000);
    auto result = gate.filter({ aa }, 10000);

    REQUIRE(result.size() == 1);
    CHECK(!result[0].approved);
}

TEST_CASE("RiskGate: order exceeding max_notional is rejected", "[riskgate]") {
    PositionLedger ledger;
    RiskGate gate(ledger, "T");
    RngService rng(42);

    RiskLimits rl;
    rl.max_position = 10000;
    rl.max_notional = 1000;  // tiny notional cap

    TestAgent agent(1, rl, rng);
    // qty=10, price=200 → notional=2000 > 1000
    auto aa = make_aa(&agent, Side::Buy, 10, 200);
    auto result = gate.filter({ aa }, 200);

    REQUIRE(result.size() == 1);
    CHECK(!result[0].approved);
}

TEST_CASE("RiskGate: margin call replaces action with zeroing market order", "[riskgate]") {
    PositionLedger ledger;
    RiskGate gate(ledger, "T");
    RngService rng(42);

    // Seed a long position with very negative unrealized PnL
    ledger.seed(/*agent=*/1, "T", /*qty=*/100, /*avg_cost=*/10000);
    // Realized pnl = 0, unrealized at mark=5000: 100*(5000-10000) = -500,000 ticks
    // margin_req_pct = 0.10 → required margin = 0.10 * 100 * 5000 = 50,000
    // equity = -500,000 < 50,000 → margin call

    RiskLimits rl;
    rl.max_position  = 10000;
    rl.max_notional  = 1'000'000'000LL;
    rl.margin_req_pct = 0.10;

    TestAgent agent(1, rl, rng);
    auto aa = make_aa(&agent, Side::Buy, 5, 5000);
    auto result = gate.filter({ aa }, 5000);

    REQUIRE(result.size() == 1);
    CHECK(result[0].approved);
    CHECK(result[0].margin_call);

    auto* so = std::get_if<SubmitOrder>(&result[0].action);
    REQUIRE(so != nullptr);
    CHECK(so->type == OrderType::Market);
    CHECK(so->side == Side::Sell);  // long → sell to flatten
    CHECK(so->qty == 100);          // full position
}

TEST_CASE("RiskGate: cancel orders always approved", "[riskgate]") {
    PositionLedger ledger;
    RiskGate gate(ledger, "T");
    RngService rng(42);

    RiskLimits rl;
    rl.max_position = 0;  // would reject any submit

    TestAgent agent(1, rl, rng);
    AgentAction aa { &agent, CancelOrder{ 42, "T" } };
    auto result = gate.filter({ aa }, 10000);

    REQUIRE(result.size() == 1);
    CHECK(result[0].approved);
}
