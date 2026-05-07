#pragma once
// UI application: reads from SnapshotBuffer (lock-free), never touches sim internals.
#include "marketdata/SnapshotBuffer.h"
#include "sim/SimulationLoop.h"
#include <SFML/Graphics.hpp>
#include <vector>
#include <cstdint>

class VisualApp {
public:
    VisualApp(SnapshotBuffer& buf, SimulationLoop& sim);
    void run();

private:
    SnapshotBuffer& buf_;
    SimulationLoop& sim_;
    sf::RenderWindow window_;

    // History for price line chart (mid_price as double)
    std::vector<double> price_history_;
    std::vector<double> tick_history_;
    static constexpr int kMaxHistory = 2000;

    void draw_controls(bool& paused);
    void draw_price_panel(const MarketSnapshot& snap);
    void draw_microstructure_panel(const MarketSnapshot& snap);
};
