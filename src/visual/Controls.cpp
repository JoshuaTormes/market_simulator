#include "Controls.h"
#include "imgui.h"
#include <string>

void Controls::draw() {
    // ── Pause / Resume ────────────────────────────────────────────────────────
    if (sim_.is_paused()) {
        if (ImGui::Button("Resume")) sim_.resume();
    } else {
        if (ImGui::Button("Pause"))  sim_.pause();
    }
    ImGui::SameLine();

    // ── Step 1 ────────────────────────────────────────────────────────────────
    if (ImGui::Button("Step 1")) {
        if (!sim_.is_paused()) sim_.pause();
        sim_.step(1);
    }
    ImGui::SameLine();

    // ── Step N ────────────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(60.0f);
    ImGui::InputInt("##stepn", &step_n_, 0, 0);
    if (step_n_ < 1) step_n_ = 1;
    ImGui::SameLine();
    if (ImGui::Button("Step N")) {
        if (!sim_.is_paused()) sim_.pause();
        sim_.step(static_cast<Tick>(step_n_));
    }
    ImGui::SameLine();

    // ── Speed ─────────────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::SliderInt("Delay µs", &delay_us_, 0, 50000))
        sim_.set_tick_delay_us(static_cast<uint64_t>(delay_us_));
    ImGui::SameLine();

    // ── Tick counter ──────────────────────────────────────────────────────────
    ImGui::Text("| Tick: %llu", (unsigned long long)sim_.current_tick());
    ImGui::SameLine();

    // ── Inject News ──────────────────────────────────────────────────────────
    if (ImGui::Button("Inject News"))
        ImGui::OpenPopup("InjectNewsModal");

    if (ImGui::BeginPopupModal("InjectNewsModal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Inject a manual news event into the simulation.");
        ImGui::Separator();
        ImGui::SliderFloat("Impact (log-ret)", &news_impact_,  -0.10f, 0.10f, "%.4f");
        ImGui::SliderFloat("Duration (ticks)", &news_duration_,  1.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("Dispersion σ",     &news_disp_,      0.0f,   1.0f, "%.2f");
        ImGui::Separator();
        if (ImGui::Button("Inject", ImVec2(100, 0))) {
            NewsEvent ev;
            ev.announce_tick      = sim_.current_tick();
            ev.ticker             = "MAIN";
            ev.impact_log_return  = static_cast<double>(news_impact_);
            ev.duration_ticks     = static_cast<double>(news_duration_);
            ev.dispersion_sigma   = static_cast<double>(news_disp_);
            sim_.inject_news(ev);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // ── Reload Population (stub — Phase 10+) ─────────────────────────────────
    if (ImGui::Button("Reload Pop")) {
        // Hot-reload of population config available in Phase 10.
    }
}
