#pragma once
#include <imgui/imgui.h>

namespace ImGui {
    void VolumeMeter(float avg, float peak, const float val_min, const float val_max, const ImVec2& size_arg = ImVec2(0, 0));
}