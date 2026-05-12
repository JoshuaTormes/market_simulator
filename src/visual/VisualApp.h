#pragma once
// UI application: reads from SnapshotBuffer (lock-free), AgentStateBuffer, TradeTapeBuffer.
// Never touches sim internals directly — all data flows through thread-safe buffers.
#include "marketdata/SnapshotBuffer.h"
#include "marketdata/TradeTapeBuffer.h"
#include "marketdata/AgentStateBuffer.h"
#include "sim/SimulationLoop.h"
#include "core/EventBus.h"
#include "economics/IFundamentalValueProcess.h"
#include "panels/OrderBookHeatmap.h"
#include "panels/CandleVolumeVwap.h"
#include "panels/TradeTape.h"
#include "panels/AgentInspector.h"
#include "panels/MicrostructureDashboard.h"
#include "panels/StylizedFactsPanel.h"
#include "panels/RegimeIndicator.h"
#include "Controls.h"
#include <SFML/Graphics.hpp>

class VisualApp {
public:
    VisualApp(SnapshotBuffer&          snap_buf,
              TradeTapeBuffer&         tape_buf,
              AgentStateBuffer&        agent_buf,
              SimulationLoop&          sim,
              EventBus&                bus,
              IFundamentalValueProcess& fundamental);

    void run();

private:
    SnapshotBuffer&           snap_buf_;
    IFundamentalValueProcess& fundamental_;

    sf::RenderWindow window_;

    // Panels
    OrderBookHeatmap      heatmap_panel_;
    CandleVolumeVwap      candle_panel_;
    TradeTape             tape_panel_;
    AgentInspector        agent_panel_;
    MicrostructureDashboard micro_panel_;
    StylizedFactsPanel    facts_panel_;
    RegimeIndicator       regime_panel_;
    Controls              controls_;
};
