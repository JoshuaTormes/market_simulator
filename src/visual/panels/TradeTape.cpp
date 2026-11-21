#include "TradeTape.h"
#include "imgui.h"
#include <cstdio>

void TradeTape::draw() {
    auto entries = buf_.snapshot();
    ImGui::Text("Trades: %d", (int)entries.size());

    if (ImGui::BeginChild("##tapelist", ImVec2(-1, -1), false)) {
        // Show newest first.
        for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
            const auto& e = entries[static_cast<size_t>(i)];
            bool is_buy = (e.taker_side == Side::Buy);
            ImVec4 col = is_buy
                ? ImVec4(0.2f, 0.85f, 0.2f, 1.0f)   // green = buy aggressor
                : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);   // red  = sell aggressor
            char buf[80];
            std::snprintf(buf, sizeof(buf),
                "t=%-6llu  P=%-8lld  Q=%-6lld  %s",
                (unsigned long long)e.tick,
                (long long)e.price,
                (long long)e.qty,
                is_buy ? "BUY " : "SELL");
            ImGui::TextColored(col, "%s", buf);
        }
    }
    ImGui::EndChild();
}
