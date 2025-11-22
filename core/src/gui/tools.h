#pragma once
#include <imgui/imgui.h>

enum ImGuiWrapMouseFlags_ {
    ImGuiWrapMouseFlags_Horizontal = 1 << 0,
    ImGuiWrapMouseFlags_Vertical = 1 << 1,

    ImGuiWrapMouseFlags_Both = ImGuiWrapMouseFlags_Horizontal | ImGuiWrapMouseFlags_Vertical,
};
typedef int ImGuiWrapMouseFlags;

namespace ImGui {
    bool WrapMousePosEx(ImGuiWrapMouseFlags axes_mask, const ImRect& wrap_rect);
    bool WrapMousePos(ImGuiWrapMouseFlags axes_mask);
}