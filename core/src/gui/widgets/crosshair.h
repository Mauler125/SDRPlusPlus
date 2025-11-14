#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace ImGui {
    void DrawCrosshairUnderCursor(const ImRect& clip, const ImU32 color = IM_COL32(255, 255, 255, 255), const float thickness = 1.0f);
}