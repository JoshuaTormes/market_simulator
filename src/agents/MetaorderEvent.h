#pragma once
// Published on the EventBus by InstitutionalExecutor when a parent order is
// finished (fully submitted, or its residual dropped), one tick after the last
// child so that child's fill is already in the desk's inventory.
#include "core/Types.h"

struct MetaorderEvent {
    AgentId agent         = 0;
    Side    side          = Side::Buy;
    Tick    tick_start    = 0;
    Tick    tick_end      = 0;
    Qty     qty_requested = 0;
    Qty     qty_submitted = 0;
    Qty     qty_executed  = 0;
};
