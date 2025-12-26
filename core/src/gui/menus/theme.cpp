#include <gui/menus/theme.h>
#include <gui/gui.h>
#include <core.h>
#include <gui/style.h>

namespace thememenu {
    int themeId;
    std::vector<std::string> themeNames;
    std::string themeNamesTxt;

    void init(const std::string& resDir) {
        // TODO: Not hardcode theme directory
        gui::themeManager.loadThemesFromDir(resDir + "/themes/");
        core::configManager.acquire();
        std::string selectedThemeName = core::configManager.conf["theme"];
        core::configManager.release();

        // Select theme by name, if not available, apply Classic theme
        themeNames = gui::themeManager.getThemeNames();
        auto it = std::find(themeNames.begin(), themeNames.end(), selectedThemeName);
        if (it == themeNames.end()) {
            it = std::find(themeNames.begin(), themeNames.end(), "Classic");
            selectedThemeName = "Classic";
        }
        themeId = std::distance(themeNames.begin(), it);
        applyTheme();

        // Apply scaling
        ImGui::GetStyle().ScaleAllSizes(style::uiScale);

        themeNamesTxt.clear();
        for (auto name : themeNames) {
            themeNamesTxt += name;
            themeNamesTxt += '\0';
        }
    }

     void applyTheme() {
         if (themeNames.empty()) {
             return;
         }

         gui::themeManager.applyTheme(themeNames[themeId]);
     }

    void draw() {
        ImGui::LeftLabel("Theme");
        ImGui::FillWidth();
        if (ImGui::Combo("##theme_select_combo", &themeId, themeNamesTxt.c_str())) {
            applyTheme();
            core::configManager.acquire();
            core::configManager.conf["theme"] = themeNames[themeId];
            core::configManager.release(true);
        }

        ImGui::Text("Theme Author: %s", gui::themeManager.getThemeAuthor().c_str());
    }
}