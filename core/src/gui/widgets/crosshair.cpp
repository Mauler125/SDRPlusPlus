#include "crosshair.h"
#include <algorithm>
#include <imgui/imgui_internal.h>

namespace ImGui {
    void DrawCrosshairUnderCursor(const ImRect& clip, const ImU32 color, const float thickness) {
        ImDrawList* const draw = ImGui::GetWindowDrawList();
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        if (!clip.Contains(mouse)) {
            return;
        }

        draw->AddLine(ImVec2(clip.Min.x, mouse.y), ImVec2(clip.Max.x, mouse.y), color, thickness);
        draw->AddLine(ImVec2(mouse.x, clip.Min.y), ImVec2(mouse.x, clip.Max.y), color, thickness);
    }
}
