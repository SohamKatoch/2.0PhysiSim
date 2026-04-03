#include "ui/UiTheme.h"

#include <imgui.h>

namespace physisim::ui {

void applyPhysiSimTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;

    s.WindowRounding = 10.f;
    s.ChildRounding = 8.f;
    s.FrameRounding = 8.f;
    s.PopupRounding = 10.f;
    s.ScrollbarRounding = 10.f;
    s.GrabRounding = 6.f;
    s.TabRounding = 7.f;

    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.IndentSpacing = 22.f;
    s.ScrollbarSize = 14.f;
    s.GrabMinSize = 12.f;
    s.WindowBorderSize = 1.f;
    s.ChildBorderSize = 1.f;
    s.PopupBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.TabBorderSize = 0.f;

    const ImVec4 bg0 = ImVec4(0.11f, 0.11f, 0.12f, 1.f);
    const ImVec4 bg1 = ImVec4(0.14f, 0.14f, 0.16f, 1.f);
    const ImVec4 bg2 = ImVec4(0.18f, 0.18f, 0.20f, 1.f);
    const ImVec4 line = ImVec4(1.f, 1.f, 1.f, 0.08f);
    const ImVec4 text = ImVec4(0.94f, 0.94f, 0.96f, 1.f);
    const ImVec4 textDim = ImVec4(0.62f, 0.62f, 0.66f, 1.f);
    const ImVec4 accent = ImVec4(0.35f, 0.55f, 0.98f, 1.f);
    const ImVec4 accentHover = ImVec4(0.45f, 0.62f, 1.f, 1.f);
    const ImVec4 accentActive = ImVec4(0.28f, 0.48f, 0.95f, 1.f);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = bg0;
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = bg2;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.25f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.30f, 1.f);
    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = bg0;
    c[ImGuiCol_TitleBgCollapsed] = bg0;
    c[ImGuiCol_MenuBarBg] = bg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.18f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1.f, 1.f, 1.f, 0.12f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.f, 1.f, 1.f, 0.20f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1.f, 1.f, 1.f, 0.28f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentActive;
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.26f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.28f, 0.33f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.32f, 0.38f, 1.f);
    c[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.30f, 0.85f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.36f, 0.90f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.34f, 0.42f, 1.f);
    c[ImGuiCol_Separator] = line;
    c[ImGuiCol_SeparatorHovered] = ImVec4(1.f, 1.f, 1.f, 0.14f);
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(1.f, 1.f, 1.f, 0.06f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(1.f, 1.f, 1.f, 0.14f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.18f, 1.f);
    c[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.24f, 0.28f, 1.f);
    c[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.24f, 1.f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.14f, 0.14f, 0.16f, 1.f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.18f, 0.21f, 1.f);

    ImGui::GetIO().FontGlobalScale = 1.05f;
}

} // namespace physisim::ui
