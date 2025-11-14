#include "crosshair.h"
#include <algorithm>

namespace ImGui {
    void DrawCrosshairUnderCursor(const ImRect& clip, const ImU32 color, const float thickness, const ImGuiCrosshairFlags flags) {
        if ((flags & ImGuiCrosshairFlags_CullAllMask) == ImGuiCrosshairFlags_CullAllMask)
            return;

        const ImVec2& mouse = ImGui::GetIO().MousePos;

        if (!clip.Contains(ImGui::GetIO().MousePos)) {
            return;
        }

        ImDrawList* const draw = ImGui::GetWindowDrawList();

        if (!(flags & ImGuiCrosshairFlags_CullHorizontal)) {
            draw->AddLine(ImVec2(clip.Min.x, mouse.y), ImVec2(clip.Max.x, mouse.y), color, thickness);
        }

        if (!(flags & ImGuiCrosshairFlags_CullVertical)) {
            draw->AddLine(ImVec2(mouse.x, clip.Min.y), ImVec2(mouse.x, clip.Max.y), color, thickness);
        }
    }
}
