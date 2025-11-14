#include "crosshair.h"
#include <algorithm>
#include <imgui/imgui_internal.h>

namespace ImGui {
    void DrawCrosshairUnderCursor(const ImU32 color, const float thickness) {
        const ImGuiWindow* const window = ImGui::GetCurrentWindow();

        if (!window) {
            return;
        }

        ImDrawList* const draw = ImGui::GetWindowDrawList();
        const ImGuiIO& io = ImGui::GetIO();

        const ImVec2 mouse = io.MousePos;
        const ImRect clip = window->InnerRect;

        if (!clip.Contains(mouse)) {
            return;
        }

        draw->AddLine(ImVec2(clip.Min.x, mouse.y), ImVec2(clip.Max.x, mouse.y), color, thickness);
        draw->AddLine(ImVec2(mouse.x, clip.Min.y), ImVec2(mouse.x, clip.Max.y), color, thickness);
    }
}
