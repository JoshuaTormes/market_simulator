#include "VisualApp.h"
#include "imgui.h"
#include "imgui-SFML.h"
#include "implot.h"
#include <algorithm>

VisualApp::VisualApp(Engine& engine)
    : engine(engine),
      window(sf::VideoMode(1280, 720), "Market Simulation"),
      tick(0) {

    window.setFramerateLimit(60);
    ImGui::SFML::Init(window);
    ImPlot::CreateContext();
    ImPlot::StyleColorsDark();
}

void VisualApp::run() {
    sf::Clock deltaClock;
    static bool paused = false;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(event);
            if (event.type == sf::Event::Closed)
                window.close();
        }

        ImGui::SFML::Update(window, deltaClock.restart());

        ImGui::Begin("Market");

        if (paused) {
            if (ImGui::Button("Resume")) {
                paused = false;
                engine.resume();
            }
        } else {
            if (ImGui::Button("Pause")) {
                paused = true;
                engine.pause();
            }
        }

        const auto& candles = engine.getCandles("AAPL");
        int total = candles.size();
        int maxVisible = 2000;
        int start = total > maxVisible ? total - maxVisible : 0;

        int hoveredIndex = -1;

        if (ImPlot::BeginPlot("Price", ImVec2(-1, 350))) {
            ImPlot::SetupAxes("Tick", "Price", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

            if (ImPlot::IsPlotHovered()) {
                ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                int idx = int(mouse.x + 0.5);
                if (idx >= start && idx < total)
                    hoveredIndex = idx;
            }

            for (int i = start; i < total; i++) {
                const auto& c = candles[i];

                ImVec4 color = c.close >= c.open
                    ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                    : ImVec4(0.8f, 0.2f, 0.2f, 1.0f);

                double x[2] = { (double)c.startTick, (double)c.startTick };
                double wick[2] = { c.low, c.high };
                double body[2] = { c.open, c.close };

                ImPlot::SetNextLineStyle(color);
                ImPlot::PlotLine("##wick", x, wick, 2);

                ImPlot::SetNextLineStyle(color, 4.0f);
                ImPlot::PlotLine("##body", x, body, 2);
            }

            if (hoveredIndex != -1) {
                const auto& c = candles[hoveredIndex];
                ImGui::BeginTooltip();
                ImGui::Text("Tick: %llu", c.startTick);
                ImGui::Text("Open: %.2f", c.open);
                ImGui::Text("High: %.2f", c.high);
                ImGui::Text("Low : %.2f", c.low);
                ImGui::Text("Close: %.2f", c.close);
                ImGui::Text("Volume: %.0f", c.volume);
                ImGui::EndTooltip();
            }

            ImPlot::EndPlot();
        }

        if (ImPlot::BeginPlot("Volume", ImVec2(-1, 200))) {
            ImPlot::SetupAxes("Tick", "Volume", ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_AutoFit);

            static std::vector<double> xs;
            static std::vector<double> vs;

            xs.clear();
            vs.clear();

            for (int i = start; i < total; i++) {
                xs.push_back((double)candles[i].startTick);
                vs.push_back((double)candles[i].volume);
            }

            if (xs.size() > 1) {
                double barWidth = xs[1] - xs[0];
                ImPlot::PlotBars("##vol", xs.data(), vs.data(), xs.size(), barWidth * 0.8);
            }

            ImPlot::EndPlot();
        }


        ImGui::End();

        window.clear();
        ImGui::SFML::Render(window);
        window.display();
    }

    ImPlot::DestroyContext();
    ImGui::SFML::Shutdown();
}
