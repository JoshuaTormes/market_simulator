#include <catch_amalgamated.hpp>
#include "core/EventBus.h"
#include <vector>
#include <string>

struct PriceEvent  { double price; };
struct VolumeEvent { int    qty;   };

TEST_CASE("EventBus — subscribers receive correct type", "[eventbus]") {
    EventBus bus;
    std::vector<double> prices;
    std::vector<int>    qtys;

    bus.subscribe<PriceEvent> ([&](const PriceEvent&  e){ prices.push_back(e.price); });
    bus.subscribe<VolumeEvent>([&](const VolumeEvent& e){ qtys.push_back(e.qty);     });

    bus.publish(PriceEvent{100.5});
    bus.publish(VolumeEvent{42});
    bus.publish(PriceEvent{101.0});

    REQUIRE(prices.size() == 2);
    REQUIRE(prices[0] == Catch::Approx(100.5));
    REQUIRE(prices[1] == Catch::Approx(101.0));
    REQUIRE(qtys.size() == 1);
    REQUIRE(qtys[0] == 42);
}

TEST_CASE("EventBus — multiple subscribers on same type all fire", "[eventbus]") {
    EventBus bus;
    int count = 0;
    bus.subscribe<PriceEvent>([&](const PriceEvent&){ ++count; });
    bus.subscribe<PriceEvent>([&](const PriceEvent&){ ++count; });

    bus.publish(PriceEvent{1.0});
    REQUIRE(count == 2);
}

TEST_CASE("EventBus — publish with no subscribers is a no-op", "[eventbus]") {
    EventBus bus;
    REQUIRE_NOTHROW(bus.publish(PriceEvent{0.0}));
}

TEST_CASE("EventBus — clear removes all handlers", "[eventbus]") {
    EventBus bus;
    int count = 0;
    bus.subscribe<PriceEvent>([&](const PriceEvent&){ ++count; });
    bus.clear();
    bus.publish(PriceEvent{1.0});
    REQUIRE(count == 0);
}

TEST_CASE("EventBus — delivery order is subscription order", "[eventbus]") {
    EventBus bus;
    std::vector<int> order;
    bus.subscribe<PriceEvent>([&](const PriceEvent&){ order.push_back(1); });
    bus.subscribe<PriceEvent>([&](const PriceEvent&){ order.push_back(2); });
    bus.subscribe<PriceEvent>([&](const PriceEvent&){ order.push_back(3); });

    bus.publish(PriceEvent{0.0});
    REQUIRE(order == std::vector<int>{1, 2, 3});
}
