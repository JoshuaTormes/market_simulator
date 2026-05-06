#pragma once
#include "core/Types.h"

struct Trade {
    Price   price       = 0;    // maker price (resting order)
    Qty     qty         = 0;
    OrderId maker_id    = 0;
    OrderId taker_id    = 0;
    Side    taker_side  = Side::Buy;
    AgentId maker_agent = 0;
    AgentId taker_agent = 0;
    Tick    tick        = 0;
    SeqNo   seq_no      = 0;
    Price   fee_maker   = 0;    // negative = rebate
    Price   fee_taker   = 0;
};
