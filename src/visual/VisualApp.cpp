#include "VisualApp.h"
#include "imgui.h"
#include "imgui-SFML.h"
#include "implot.h"
#include <cstdio>
#include <vector>
#include <algorithm>

VisualApp::VisualApp(SnapshotBuffer& buf, SimulationLoop& sim)
    : buf_(buf)
    , sim_(sim)
    , window_(sf::VideoMode(1400, 800), "Market Microstructure Simulator")
{
    window_.setFramerateLimit(60);
    bool ok = ImGui::SFML::Init(window_);
    (void)ok;
    ImPlot::CreateContext();
    ImPlot::StyleColorsDark();
}

void VisualApp::run() {
    sf::Clock delta_clock;
    bool paused = false;

    while (window_.isOpen()) {
        sf::Event event;
        while (window_.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(event);
            if (event.type == sf::Event::Closed)
                window_.close();
        }

        ImGui::SFML::Update(window_, delta_clock.restart());

        MarketSnapshot snap;
        bool has_snap = buf_.get_latest(snap);

        if (has_snap) {
            if (static_cast<int>(price_history_.size()) >= kMaxHistory) {
                price_history_.erase(price_history_.begin());
                tick_history_.erase(tick_history_.begin());
            }
            price_history_.push_back(static_cast<double>(snap.mid_price));
            tick_history_.push_back(static_cast<double>(snap.tick));
        }

        draw_controls(paused);
        if (has_snap) {
            draw_price_panel(snap);
            draw_microstructure_panel(snap);
        }

        window_.clear(sf::Color(20, 20, 20));
        ImGui::SFML::Render(window_);
        window_.display();
    }

    ImPlot::DestroyContext();
    ImGui::SFML::Shutdown();
}

void VisualApp::draw_controls(bool& paused) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, 80), ImGuiCond_Always);
    ImGui::Begin("Controls", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    if (paused) {
        if (ImGui::Button("Resume")) { paused = false; sim_.resume(); }
    } else {
        if (ImGui::Button("Pause"))  { paused = true;  sim_.pause();  }
    }
    ImGui::SameLine();
    ImGui::Text("Tick: %llu", (unsigned long long)sim_.current_tick());
    ImGui::End();
}

void VisualApp::draw_price_panel(const MarketSnapshot& snap) {
    ImGui::SetNextWindowPos(ImVec2(0, 80), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(700, 360), ImGuiCond_Always);
    ImGui::Begin("Price", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "Mid: %lld  Spread: %lld",
                  (long long)snap.mid_price, (long long)snap.spread);
    ImGui::Text("%s", buf);

    if (!price_history_.empty() && ImPlot::BeginPlot("##price", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Tick", "Mid Price",
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::PlotLine("Mid", tick_history_.data(), price_history_.data(),
                         (int)price_history_.size());
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void VisualApp::draw_microstructure_panel(const MarketSnapshot& snap) {
    ImGui::SetNextWindowPos(ImVec2(700, 80), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(700, 360), ImGuiCond_Always);
    ImGui::Begin("Microstructure", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    ImGui::Text("Vol(60t): %.5f  OFI: %.4f  Momentum: %.4f",
                snap.realized_vol[0], snap.order_flow_imbalance, snap.momentum);
    ImGui::Text("Imbalance(L1): %.3f  TradeImb: %.3f",
                snap.book_imbalance[0], snap.trade_imbalance);

    static std::vector<double> vol_hist;
    static std::vector<double> spread_hist;
    static std::vector<double> t_hist;

    while (static_cast<int>(vol_hist.size()) >= kMaxHistory) {
        vol_hist.erase(vol_hist.begin());
        spread_hist.erase(spread_hist.begin());
        t_hist.erase(t_hist.begin());
    }
    vol_hist.push_back(snap.realized_vol[0]);
    spread_hist.push_back(static_cast<double>(snap.spread));
    t_hist.push_back(static_cast<double>(snap.tick));

    if (ImPlot::BeginPlot("##vol_spread", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Tick", nullptr,
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis(ImAxis_Y2, "Spread", ImPlotAxisFlags_AuxDefault);
        ImPlot::PlotLine("Realized Vol",  t_hist.data(), vol_hist.data(),    (int)vol_hist.size());
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        ImPlot::PlotLine("Spread",        t_hist.data(), spread_hist.data(), (int)spread_hist.size());
        ImPlot::EndPlot();
    }
    ImGui::End();
}
