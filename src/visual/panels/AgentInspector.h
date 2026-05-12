#pragma once
// Per-agent position inspector: dropdown → qty / realized PnL / unrealized PnL.
#include "marketdata/AgentStateBuffer.h"

class AgentInspector {
public:
    explicit AgentInspector(AgentStateBuffer& buf) : buf_(buf) {}
    void draw();

private:
    AgentStateBuffer& buf_;
    int selected_ = 0;
};
