#include "ui.h"
#include "../version.h"
#include "game_addresses.h"
#include "hitbox_capture.h"
#include "keyboard_input.h"
#include "locale.h"
#include "overlay.h"
#include "practice_menu.h"
#include "imgui.h"

#include <windows.h>
#include <algorithm>

void DrawPracticeBaseUi(const char* rendererName)
{
    static bool showDemo = false;
    static bool invincibilityPlaceholder = false;
    static float gameSpeedPlaceholder = 60.0f;

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin(S(BaseTitle), nullptr, flags);
    ImGui::TextUnformatted(S(InjectionActive));
    ImGui::Text(S(Version), Th06ncPracVersion::Text);
    ImGui::Text(S(Renderer), rendererName);
    ImGui::Text(S(ProcessId), GetCurrentProcessId());
    ImGui::TextDisabled("%s", S(F10Hint));
    ImGui::Separator();

    static const char* languageNames[] = {"中文", "English", "日本語"};
    int language = static_cast<int>(Locale::Instance().GetLanguage());
    if (ImGui::Combo(S(Language), &language, languageNames,
            IM_ARRAYSIZE(languageNames)))
        Locale::Instance().SetLanguage(static_cast<Language>(language));

    if (IsGameProcessForeground() &&
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 &&
        (GetAsyncKeyState('D') & 1) != 0)
        SetAutoShootEnabled(true);

    ImGui::Checkbox(S(ShowDemo), &showDemo);
    bool autoShoot = IsAutoShootEnabled();
    if (ImGui::Checkbox(S(AutoShoot), &autoShoot))
        SetAutoShootEnabled(autoShoot);
    const auto* practiceFlag =
        ResolveGameAddress<uint8_t>(GameAddress::PracticeModeFlag);
    const bool practiceActive = IsEnhancedPracticeRunActive() ||
        (practiceFlag && *practiceFlag != 0);
    bool showHitboxes = IsHitboxDisplayEnabled();
    if (practiceActive) {
        if (ImGui::Checkbox(S(ShowHitboxes), &showHitboxes))
            SetHitboxDisplayEnabled(showHitboxes);
    } else {
        ImGui::TextDisabled("[ ] %s", S(ShowHitboxes));
    }
    bool stretchMode = IsGameStretchModeEnabled();
    if (ImGui::Checkbox(S(ApplyStretch), &stretchMode))
        SetGameStretchModeEnabled(stretchMode);
    ImGui::TextDisabled("%s", S(StretchHelp));
    // DrawHitboxSettingsUi();
    ImGui::Separator();
    //ImGui::TextDisabled("Practice feature placeholders");
    // 
    // ImGui::Checkbox("Invincibility (not wired yet)", &invincibilityPlaceholder);
    ImGui::SetNextItemWidth(300.0f);
    ImGui::DragFloat(S(GameSpeed), &gameSpeedPlaceholder, 1.0f, 60.0f, 1800.0f, "%.2f fps");
    ImGui::SameLine();
    if (ImGui::Button(S(Ok))){
        gameSpeedPlaceholder = std::clamp(gameSpeedPlaceholder, 60.0f, 1800.0f);
        auto* timerPeriod = ResolveGameAddress<INT64>(GameAddress::GameTimerPeriod);
        const auto* timerFrequency = ResolveGameAddress<INT64>(GameAddress::GameTimerFrequency);
        if (timerPeriod && timerFrequency)
            *timerPeriod = static_cast<INT64>(
                static_cast<double>(*timerFrequency) / gameSpeedPlaceholder + 0.5);
    }
    ImGui::SameLine();
    if (ImGui::Button("1.0x")) {
        gameSpeedPlaceholder = 60;
        auto* timerPeriod = ResolveGameAddress<INT64>(GameAddress::GameTimerPeriod);
        const auto* timerFrequency = ResolveGameAddress<INT64>(GameAddress::GameTimerFrequency);
        if (timerPeriod && timerFrequency)
            *timerPeriod = static_cast<INT64>(
                static_cast<double>(*timerFrequency) / gameSpeedPlaceholder + 0.5);
    }
    ImGui::SameLine();
    if (ImGui::Button("1.25x")) {
        gameSpeedPlaceholder = 75;
        auto* timerPeriod = ResolveGameAddress<INT64>(GameAddress::GameTimerPeriod);
        const auto* timerFrequency = ResolveGameAddress<INT64>(GameAddress::GameTimerFrequency);
        if (timerPeriod && timerFrequency)
            *timerPeriod = static_cast<INT64>(
                static_cast<double>(*timerFrequency) / gameSpeedPlaceholder + 0.5);
    }
    ImGui::SameLine();
    if (ImGui::Button("1.5x")) {
        gameSpeedPlaceholder = 90;
        auto* timerPeriod = ResolveGameAddress<INT64>(GameAddress::GameTimerPeriod);
        const auto* timerFrequency = ResolveGameAddress<INT64>(GameAddress::GameTimerFrequency);
        if (timerPeriod && timerFrequency)
            *timerPeriod = static_cast<INT64>(
                static_cast<double>(*timerFrequency) / gameSpeedPlaceholder + 0.5);
    }

    ImGui::NewLine();
    ImGui::NewLine();
    ImGui::NewLine();

    if (ImGui::CollapsingHeader(S(Licenses))) {
        ImGui::TextWrapped("th06nc_prac_vibe - MIT License - Copyright (c) 2026 RUEEE");
        ImGui::Separator();
        ImGui::TextWrapped("Dear ImGui - MIT License - Copyright (c) 2014-2021 Omar Cornut");
        ImGui::TextWrapped("stb libraries bundled by Dear ImGui - MIT License or Public Domain - Copyright (c) 2017 Sean Barrett");
        ImGui::Separator();
        ImGui::TextWrapped("This project references thprac, released under the MIT License - Copyright (c) 2022 Ack and its contributors.");
        ImGui::TextDisabled("See LICENSE, THIRD_PARTY_NOTICES.md, and third_party/imgui/LICENSE.txt.");
    }
    ImGui::End();

    if (showDemo)
        ImGui::ShowDemoWindow(&showDemo);
}
