#include <gui/widgets/volume_meter.h>
#include <algorithm>
#include <imgui/imgui_internal.h>

namespace ImGui {
    static inline ImU32 LerpColU32(const ImU32 a, const ImU32 b, const float t) {
        const ImU32 a_r = (a >> IM_COL32_R_SHIFT) & 0xFF;
        const ImU32 a_g = (a >> IM_COL32_G_SHIFT) & 0xFF;
        const ImU32 a_b = (a >> IM_COL32_B_SHIFT) & 0xFF;
        const ImU32 a_a = (a >> IM_COL32_A_SHIFT) & 0xFF;
        const ImU32 b_r = (b >> IM_COL32_R_SHIFT) & 0xFF;
        const ImU32 b_g = (b >> IM_COL32_G_SHIFT) & 0xFF;
        const ImU32 b_b = (b >> IM_COL32_B_SHIFT) & 0xFF;
        const ImU32 b_a = (b >> IM_COL32_A_SHIFT) & 0xFF;

        const float ts = t + 0.5f;
        const ImU32 r = ImLerp(a_r, b_r, ts);
        const ImU32 g = ImLerp(a_g, b_g, ts);
        const ImU32 bcol = ImLerp(a_b, b_b, ts);
        const ImU32 alpha = ImLerp(a_a, b_a, ts);

        return IM_COL32(r, g, bcol, alpha);
    }

    void VolumeMeter(float avg, float peak, const float val_min, const float val_max, const ImVec2& size_arg) {
        const ImGuiWindow* const window = GetCurrentWindow();
        const ImGuiContext* const context = GetCurrentContext();
        const ImGuiStyle& style = GetStyle();

        avg = ImClamp<float>(avg, val_min, val_max);
        peak = ImClamp<float>(peak, val_min, val_max);

        const ImVec2 min = window->DC.CursorPos;
        const ImVec2 size = CalcItemSize(size_arg, CalcItemWidth(), (context->FontSize / 2) + style.FramePadding.y);

        ItemSize(size, style.FramePadding.y);
        const ImRect bb(min, min + size);

        if (!ItemAdd(bb, 0)) {
            return;
        }

        const ImU32 bgDarkGreen = IM_COL32(9, 80, 9, 255);
        const ImU32 bgGreen = IM_COL32(9, 136, 9, 255);
        const ImU32 bgYellow = IM_COL32(136, 136, 9, 255);
        const ImU32 bgRed = IM_COL32(136, 9, 9, 255);

        const float greenFrac = 0.25f;
        const float yellowFrac = 0.85f;

        const float greenPointX = bb.Min.x + size.x * greenFrac;
        const float yellowPointX = bb.Min.x + size.x * yellowFrac;

        window->DrawList->AddRectFilledMultiColor(bb.Min, ImVec2(greenPointX, bb.Max.y), bgDarkGreen, bgGreen, bgGreen, bgDarkGreen);
        window->DrawList->AddRectFilledMultiColor(ImVec2(greenPointX, bb.Min.y), ImVec2(yellowPointX, bb.Max.y), bgGreen, bgYellow, bgYellow, bgGreen);
        window->DrawList->AddRectFilledMultiColor(ImVec2(yellowPointX, bb.Min.y), bb.Max, bgYellow, bgRed, bgRed, bgYellow);

        const ImU32 fgDarkGreen = IM_COL32(0, 160, 0, 255);
        const ImU32 fgGreen = IM_COL32(0, 255, 0, 255);
        const ImU32 fgYellow = IM_COL32(255, 255, 0, 255);
        const ImU32 fgRed = IM_COL32(255, 0, 0, 255);

        const float avgPx = ImClamp(((avg - val_min) / (val_max - val_min)) * size.x, 0.0f, size.x);

        if (avgPx > 0.0f) {
            window->DrawList->PushClipRect(bb.Min, ImVec2(bb.Min.x + avgPx, bb.Max.y), true);

            window->DrawList->AddRectFilledMultiColor(bb.Min, ImVec2(greenPointX, bb.Max.y), fgDarkGreen, fgGreen, fgGreen, fgDarkGreen);
            window->DrawList->AddRectFilledMultiColor(ImVec2(greenPointX, bb.Min.y), ImVec2(yellowPointX, bb.Max.y), fgGreen, fgYellow, fgYellow, fgGreen);
            window->DrawList->AddRectFilledMultiColor(ImVec2(yellowPointX, bb.Min.y), bb.Max, fgYellow, fgRed, fgRed, fgYellow);

            window->DrawList->PopClipRect();
        }

        const float peakF = ImClamp(((peak - val_min) / (val_max - val_min)) * size.x, 0.0f, size.x);
        const float peakSampleFrac = (size.x > 0.0f) ? (peakF / size.x) : 0.0f;

        ImU32 peakCol;

        if (peakSampleFrac <= greenFrac) {
            const float t = greenFrac > 0.0f ? (peakSampleFrac / greenFrac) : 1.0f;
            peakCol = LerpColU32(fgDarkGreen, fgGreen, t);
        }
        else if (peakSampleFrac <= yellowFrac) {
            const float t = (peakSampleFrac - greenFrac) / (yellowFrac - greenFrac);
            peakCol = LerpColU32(fgGreen, fgYellow, t);
        }
        else {
            const float t = (peakSampleFrac - yellowFrac) / (1.0f - yellowFrac);
            peakCol = LerpColU32(fgYellow, fgRed, t);
        }

        const float peakX = bb.Min.x + peakF;
        window->DrawList->AddLine(ImVec2(peakX, bb.Min.y), ImVec2(peakX, bb.Max.y), peakCol, 1.0f);
    }
}