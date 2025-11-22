#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <gui/tools.h>

namespace ImGui {
    // Taken from: https://github.com/ocornut/imgui/issues/228
    // Minor modifications made to return true if teleported.
    bool WrapMousePosEx(ImGuiWrapMouseFlags axes_mask, const ImRect& wrap_rect) {
        IM_ASSERT(axes_mask & ImGuiWrapMouseFlags_Both);
        ImGuiContext& g = *GImGui;
        ImVec2 p_mouse = g.IO.MousePos;
        for (int axis = 0; axis < 2; axis++) {
            if ((axes_mask & (1 << axis)) == 0) {
                continue;
            }
            if (p_mouse[axis] >= wrap_rect.Max[axis]) {
                p_mouse[axis] = wrap_rect.Min[axis] + 1.0f;
            }
            else if (p_mouse[axis] <= wrap_rect.Min[axis]) {
                p_mouse[axis] = wrap_rect.Max[axis] - 1.0f;
            }
        }
        if (p_mouse.x != g.IO.MousePos.x || p_mouse.y != g.IO.MousePos.y) {
            TeleportMousePos(p_mouse);
            return true;
        }

        return false;
    }

    // Copied from imgui.cpp
    static int FindPlatformMonitorForPos(const ImVec2& pos) {
        ImGuiContext& g = *GImGui;
        for (int monitor_n = 0; monitor_n < g.PlatformIO.Monitors.Size; monitor_n++) {
            const ImGuiPlatformMonitor& monitor = g.PlatformIO.Monitors[monitor_n];
            if (ImRect(monitor.MainPos, monitor.MainPos + monitor.MainSize).Contains(pos))
                return monitor_n;
        }
        return -1;
    }

    bool WrapMousePos(ImGuiWrapMouseFlags axes_mask) {
        ImGuiContext& g = *GImGui;
#ifdef IMGUI_HAS_DOCK
        if (g.IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            const int monitor_index = FindPlatformMonitorForPos(g.IO.MousePosPrev);
            if (monitor_index == -1) {
                return false;
            }
            const ImGuiPlatformMonitor& monitor = g.PlatformIO.Monitors[monitor_index];
            return WrapMousePosEx(axes_mask, ImRect(monitor.MainPos, monitor.MainPos + monitor.MainSize - ImVec2(1.0f, 1.0f)));
        }
        else
#endif
        {
            ImGuiViewport* viewport = GetMainViewport();
            return WrapMousePosEx(axes_mask, ImRect(viewport->Pos, viewport->Pos + viewport->Size - ImVec2(1.0f, 1.0f)));
        }
    }
}