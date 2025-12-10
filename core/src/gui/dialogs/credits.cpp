#include <gui/dialogs/credits.h>
#include <imgui.h>
#include <gui/icons.h>
#include <gui/style.h>
#include <config.h>
#include <credits.h>
#include <version.h>
#include <core.h>

namespace credits {
    ImFont* bigFont;
    ImVec2 imageSize(128.0f, 128.0f);
    bool isOpen = false;

    void init() {
        imageSize = ImVec2(128.0f * style::uiScale, 128.0f * style::uiScale);
    }

    static void begin() {
        if (!isOpen) {
            return;
        }

        const ImGuiViewport* const vp = ImGui::GetMainViewport();

        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::SetNextWindowPos(vp->Pos + vp->Size * 0.5f, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    }
    static void end() {
        if (!isOpen) {
            return;
        }

        ImGui::EndPopup();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void show() {
        begin();

        if (!ImGui::BeginPopupModal("Credits", &isOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            end();
            return;
        }

        ImGui::PushFont(style::hugeFont);
        ImGui::TextUnformatted("SDR++          ");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::Image(icons::LOGO, imageSize);
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::TextUnformatted("This software is brought to you by Alexandre Rouma (ON5RYZ) with the help of\n\n");

        ImGui::Columns(4, "CreditColumns", true);

        ImGui::TextUnformatted("Contributors");
        for (int i = 0; i < sdrpp_credits::contributorCount; i++) {
            ImGui::BulletText("%s", sdrpp_credits::contributors[i]);
        }

        ImGui::NextColumn();
        ImGui::TextUnformatted("Libraries");
        for (int i = 0; i < sdrpp_credits::libraryCount; i++) {
            ImGui::BulletText("%s", sdrpp_credits::libraries[i]);
        }

        ImGui::NextColumn();
        ImGui::TextUnformatted("Hardware Donators");
        for (int i = 0; i < sdrpp_credits::hardwareDonatorCount; i++) {
            ImGui::BulletText("%s", sdrpp_credits::hardwareDonators[i]);
        }

        ImGui::NextColumn();
        ImGui::TextUnformatted("Patrons");
        for (int i = 0; i < sdrpp_credits::patronCount; i++) {
            ImGui::BulletText("%s", sdrpp_credits::patrons[i]);
        }

        ImGui::Columns(1, "CreditColumnsEnd", true);

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::TextUnformatted(core::getBuildString());
        end();
    }
}