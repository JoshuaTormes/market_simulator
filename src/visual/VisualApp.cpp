#include "VisualApp.h"
#include "imgui.h"
#include "imgui-SFML.h"
#include "implot.h"
#include <cstdio>

VisualApp::VisualApp(SnapshotBuffer&           snap_buf,
                     TradeTapeBuffer&          tape_buf,
                     AgentStateBuffer&         agent_buf,
                     SimulationLoop&           sim,
                     EventBus&                 bus,
                     IFundamentalValueProcess& fundamental)
    : snap_buf_(snap_buf)
    , fundamental_(fundamental)
    , window_(sf::VideoMode(1400, 900), "Market Microstructure Simulator")
    , tape_panel_(tape_buf)
    , agent_panel_(agent_buf)
    , controls_(sim, bus)
{
    window_.setFramerateLimit(60);
    bool ok = ImGui::SFML::Init(window_);
    (void)ok;
    ImPlot::CreateContext();
    ImPlot::StyleColorsDark();
}

void VisualApp::run() {
    sf::Clock delta_clock;

    while (window_.isOpen()) {
        sf::Event event;
        while (window_.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(event);
            if (event.type == sf::Event::Closed)
                window_.close();
        }
        ImGui::SFML::Update(window_, delta_clock.restart());

        MarketSnapshot snap;
        bool has_snap = snap_buf_.get_latest(snap);

        float full_w = static_cast<float>(window_.getSize().x);
        float full_h = static_cast<float>(window_.getSize().y);
        constexpr float kCtrlH = 50.0f;
        constexpr ImGuiWindowFlags kFixed =
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;

        // ── Controls bar (always on top) ──────────────────────────────────────
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(full_w, kCtrlH), ImGuiCond_Always);
        ImGui::Begin("##controls", nullptr, kFixed);
        controls_.draw();
        ImGui::End();

        // ── Main tabbed area ──────────────────────────────────────────────────
        ImGui::SetNextWindowPos(ImVec2(0, kCtrlH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(full_w, full_h - kCtrlH), ImGuiCond_Always);
        ImGui::Begin("##main", nullptr,
                     kFixed | ImGuiWindowFlags_NoScrollWithMouse);

        if (ImGui::BeginTabBar("##tabs")) {

            if (ImGui::BeginTabItem("Book Heatmap")) {
                if (has_snap) heatmap_panel_.draw(snap);
                else          ImGui::TextDisabled("Waiting for first snapshot...");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Candle / VWAP")) {
                if (has_snap) candle_panel_.draw(snap);
                else          ImGui::TextDisabled("Waiting for first snapshot...");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Trade Tape")) {
                tape_panel_.draw();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Agent Inspector")) {
                agent_panel_.draw();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Microstructure")) {
                if (has_snap) micro_panel_.draw(snap);
                else          ImGui::TextDisabled("Waiting for first snapshot...");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Stylized Facts")) {
                if (has_snap) facts_panel_.draw(snap);
                else          ImGui::TextDisabled("Waiting for first snapshot...");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Regime")) {
                int    regime    = snap.regime;
                double fund_val  = fundamental_.current_value();
                double tick      = static_cast<double>(snap.tick);
                regime_panel_.draw(regime, fund_val, tick);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();

        window_.clear(sf::Color(20, 20, 20));
        ImGui::SFML::Render(window_);
        window_.display();
    }

    ImPlot::DestroyContext();
    ImGui::SFML::Shutdown();
}
