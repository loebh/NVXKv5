#ifndef NVXK_IMGUI_THEME_HPP
#define NVXK_IMGUI_THEME_HPP

#include <imgui.h>

namespace nvxk_ui {

// Semantic color palette matching NVXK dark-graphite engineering specification
struct Palette {
    static constexpr ImVec4 Bg           = ImVec4(0.067f, 0.075f, 0.082f, 1.0f); // #111315
    static constexpr ImVec4 Sidebar      = ImVec4(0.090f, 0.098f, 0.110f, 1.0f); // #17191C
    static constexpr ImVec4 Panel        = ImVec4(0.106f, 0.118f, 0.133f, 1.0f); // #1B1E22
    static constexpr ImVec4 PanelSec     = ImVec4(0.133f, 0.149f, 0.169f, 1.0f); // #22262B
    static constexpr ImVec4 Border       = ImVec4(0.188f, 0.208f, 0.231f, 1.0f); // #30353B
    static constexpr ImVec4 TextPrimary  = ImVec4(0.902f, 0.914f, 0.929f, 1.0f); // #E6E9ED
    static constexpr ImVec4 TextDim      = ImVec4(0.573f, 0.604f, 0.643f, 1.0f); // #929AA4
    static constexpr ImVec4 Green        = ImVec4(0.149f, 0.788f, 0.584f, 1.0f); // #26C995
    static constexpr ImVec4 Cyan         = ImVec4(0.220f, 0.741f, 0.973f, 1.0f); // #38BDF8
    static constexpr ImVec4 Red          = ImVec4(0.890f, 0.420f, 0.451f, 1.0f); // #E36B73
    static constexpr ImVec4 Amber        = ImVec4(0.914f, 0.706f, 0.353f, 1.0f); // #E9B45A
};

void apply_nvxk_dark_theme();

}  // namespace nvxk_ui

#endif