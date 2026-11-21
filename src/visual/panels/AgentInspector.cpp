#include "AgentInspector.h"
#include "imgui.h"
#include <cstdio>
#include <vector>
#include <string>

void AgentInspector::draw() {
    auto states = buf_.snapshot();
    if (states.empty()) {
        ImGui::TextDisabled("No agent state data yet.");
        return;
    }

    // Clamp selection to valid range.
    if (selected_ >= static_cast<int>(states.size()))
        selected_ = static_cast<int>(states.size()) - 1;

    // Agent dropdown.
    char preview[32];
    std::snprintf(preview, sizeof(preview), "Agent #%llu",
                  (unsigned long long)states[selected_].id);
    if (ImGui::BeginCombo("Agent", preview)) {
        for (int i = 0; i < static_cast<int>(states.size()); ++i) {
            char label[32];
            std::snprintf(label, sizeof(label), "Agent #%llu",
                          (unsigned long long)states[i].id);
            bool sel = (i == selected_);
            if (ImGui::Selectable(label, sel)) selected_ = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    const AgentState& s = states[selected_];
    ImGui::Text("Agent ID       : %llu", (unsigned long long)s.id);
    ImGui::Text("Net Qty        : %lld", (long long)s.net_qty);
    ImGui::Text("Realized PnL   : %lld ticks", (long long)s.realized_pnl);
    ImGui::Text("Unrealized PnL : %lld ticks", (long long)s.unrealized_pnl);
    ImGui::Separator();
    ImGui::TextDisabled("Trade history available in Phase 8 (binary log).");
}
