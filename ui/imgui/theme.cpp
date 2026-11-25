#include "theme.hpp"
#include <implot.h>

namespace nvxk_ui {

void apply_nvxk_dark_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 10.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = Palette::TextPrimary;
    c[ImGuiCol_TextDisabled]         = Palette::TextDim;
    c[ImGuiCol_WindowBg]             = Palette::Bg;
    c[ImGuiCol_ChildBg]              = Palette::Panel;
    c[ImGuiCol_PopupBg]              = Palette::PanelSec;
    c[ImGuiCol_Border]               = Palette::Border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = Palette::PanelSec;
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.17f, 0.19f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.20f, 0.23f, 0.27f, 1.0f);
    c[ImGuiCol_TitleBg]              = Palette::Sidebar;
    c[ImGuiCol_TitleBgActive]        = Palette::Sidebar;
    c[ImGuiCol_MenuBarBg]            = Palette::Sidebar;
    c[ImGuiCol_ScrollbarBg]          = Palette::Sidebar;
    c[ImGuiCol_ScrollbarGrab]        = Palette::Border;
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.31f, 0.35f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = Palette::Cyan;
    c[ImGuiCol_CheckMark]            = Palette::Cyan;
    c[ImGuiCol_SliderGrab]           = Palette::Cyan;
    c[ImGuiCol_SliderGrabActive]     = Palette::Green;
    c[ImGuiCol_Button]               = Palette::PanelSec;
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.18f, 0.21f, 0.25f, 1.0f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.22f, 0.32f, 0.42f, 1.0f);
    c[ImGuiCol_Header]               = ImVec4(0.22f, 0.74f, 0.97f, 0.16f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.22f, 0.74f, 0.97f, 0.24f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.22f, 0.74f, 0.97f, 0.32f);
    c[ImGuiCol_Separator]            = Palette::Border;
    c[ImGuiCol_Tab]                  = Palette::Sidebar;
    c[ImGuiCol_TabHovered]           = Palette::PanelSec;
    c[ImGuiCol_TabActive]            = Palette::Panel;
    c[ImGuiCol_TableHeaderBg]        = Palette::Sidebar;
    c[ImGuiCol_TableBorderStrong]    = Palette::Border;
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.15f, 0.17f, 0.19f, 1.0f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_PlotLines]            = Palette::Cyan;
    c[ImGuiCol_PlotHistogram]        = Palette::Green;

    ImPlotStyle& ps = ImPlot::GetStyle();
    ps.PlotBg = Palette::Bg;
    ps.PlotBorderSize = 1.0f;
    ps.Colors[ImPlotCol_FrameBg] = Palette::Panel;
    ps.Colors[ImPlotCol_PlotBg] = Palette::Bg;
    ps.Colors[ImPlotCol_PlotBorder] = Palette::Border;
    ps.Colors[ImPlotCol_LegendBg] = Palette::PanelSec;
    ps.Colors[ImPlotCol_LegendBorder] = Palette::Border;
}


void apply_light_theme() {
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
}

}  // namespace nvxk_ui