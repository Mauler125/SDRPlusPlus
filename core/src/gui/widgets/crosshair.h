#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace ImGui {
    enum ImGuiCrosshairFlags_ {
        ImGuiCrosshairFlags_None = 0,
        ImGuiCrosshairFlags_CullHorizontal = 1 << 0,
        ImGuiCrosshairFlags_CullVertical = 1 << 1,

        ImGuiCrosshairFlags_CullAllMask = ImGuiCrosshairFlags_CullHorizontal | ImGuiCrosshairFlags_CullVertical,
    };

    typedef int ImGuiCrosshairFlags;
    void DrawCrosshairUnderCursor(const ImRect& clip, const ImU32 color = IM_COL32(255, 255, 255, 255), 
        const float thickness = 1.0f, const ImGuiCrosshairFlags flags = ImGuiCrosshairFlags_None);
}