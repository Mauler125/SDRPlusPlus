#include <json.hpp>
#include <gui/theme_manager.h>
#include <imgui_internal.h>
#include <implot/implot.h>
#include <utils/flog.h>
#include <filesystem>
#include <fstream>

ThemeManager::ThemeManager() {
    initCoreColors();
}

void ThemeManager::initCoreColors() {
    m_colorArray[CoreCol_WaterfallBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    m_colorArray[CoreCol_ClearColor] = ImVec4(0.0666f, 0.0666f, 0.0666f, 1.0f);
    m_colorArray[CoreCol_FFTHoldColor] = ImVec4(0.0f, 1.0f, 0.75f, 1.0f);
    m_colorArray[CoreCol_CrosshairColor] = ImVec4(0.86f, 0.86f, 0.0f, 1.0f);
    m_colorArray[CoreCol_MainBorderColor] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    m_colorArray[CoreCol_WaterfallSeparatorColor] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
}

bool ThemeManager::loadThemesFromDir(const std::string& path) {
    // // TEST JUST TO DUMP THE ORIGINAL THEME
    //printf("ImGui theme:\n\n");
    //auto& imguiStyle = ImGui::GetStyle();
    //for (auto [name, id] : sm_imguiColorStringToCodeTable) {
    //    ImVec4 col = imguiStyle.Colors[id];
    //    uint8_t r = 255 - (col.x * 255.0f);
    //    uint8_t g = 255 - (col.y * 255.0f);
    //    uint8_t b = 255 - (col.z * 255.0f);
    //    uint8_t a = col.w * 255.0f;
    //    printf("\"%s\": \"#%02X%02X%02X%02X\",\n", name.data(), r, g, b, a);
    //}
    //printf("\n\n");
    //printf("ImPlot theme:\n\n");
    //auto& implotStyle = ImPlot::GetStyle();
    //for (auto [name, id] : sm_implotColorStringToCodeTable) {
    //    ImVec4 col = implotStyle.Colors[id];
    //    uint8_t r = 255 - (col.x * 255.0f);
    //    uint8_t g = 255 - (col.y * 255.0f);
    //    uint8_t b = 255 - (col.z * 255.0f);
    //    uint8_t a = col.w * 255.0f;
    //    printf("\"%s\": \"#%02X%02X%02X%02X\",\n", name.data(), r, g, b, a);
    //}
    //printf("\n\n");


    if (!std::filesystem::is_directory(path)) {
        flog::error("Theme directory doesn't exist: {0}", path);
        return false;
    }
    m_loadedThemes.clear();
    for (const auto& file : std::filesystem::directory_iterator(path)) {
        std::string _path = file.path().generic_string();
        if (file.path().extension().generic_string() != ".json") {
            continue;
        }
        loadTheme(_path);
    }
    return true;
}

bool ThemeManager::loadTheme(const std::string& path) {
    if (!std::filesystem::is_regular_file(path)) {
        flog::error("Theme file doesn't exist: {0}", path);
        return false;
    }

    // Load defaults in theme
    Theme thm;
    thm.author = "--";

    // Load JSON
    std::ifstream file(path.c_str());
    json data;
    file >> data;
    file.close();

    // Load theme name
    if (!data.contains("name")) {
        flog::error("Theme {0} is missing the name parameter", path);
        return false;
    }
    if (!data["name"].is_string()) {
        flog::error("Theme {0} contains invalid name field. Expected string", path);
        return false;
    }
    std::string name = data["name"];

    if (m_loadedThemes.find(name) != m_loadedThemes.end()) {
        flog::error("A theme named '{0}' already exists", name);
        return false;
    }

    // Load theme author if available
    if (data.contains("author")) {
        if (!data["author"].is_string()) {
            flog::error("Theme {0} contains invalid author field. Expected string", path);
            return false;
        }
        thm.author = data["author"];
    }

    // Iterate through all parameters and check their contents
    std::map<std::string, std::string> params = data;
    for (auto const& [param, val] : params) {
        if (param == "name" || param == "author") { continue; }

        bool isValid = false;

        // If param is a color, check that it's a valid RGBA hex value
        if (sm_imguiColorStringToCodeTable.find(param) != sm_imguiColorStringToCodeTable.end() ||
            sm_implotColorStringToCodeTable.find(param) != sm_implotColorStringToCodeTable.end() ||
            sm_coreColorStringToCodeTable.find(param) != sm_coreColorStringToCodeTable.end()) {
            if (val[0] != '#' || !std::all_of(val.begin() + 1, val.end(), ::isxdigit) || val.length() != 9) {
                flog::error("Theme {0} contains invalid {1} field. Expected hex RGBA color", path, param);
                return false;
            }
            isValid = true;
        }

        if (!isValid) {
            flog::error("Theme {0} contains unknown {1} field.", path, param);
            return false;
        }
    }

    thm.data = data;
    m_loadedThemes[name] = thm;

    return true;
}

bool ThemeManager::applyTheme(const std::string& name) {
    if (m_loadedThemes.find(name) == m_loadedThemes.end()) {
        flog::error("Unknown theme: {0}", name);
        return false;
    }

    ImGui::StyleColorsDark();
    ImPlot::StyleColorsClassic();
    initCoreColors();

    auto& imguiStyle = ImGui::GetStyle();
    auto& implotStyle = ImPlot::GetStyle();

    imguiStyle.WindowPadding.x = 6.0f;
    imguiStyle.WindowPadding.y = 6.0f;

    imguiStyle.ItemSpacing.x = 6.0f;
    imguiStyle.ItemSpacing.y = 4.0f;

    imguiStyle.WindowRounding = 6.0f;
    imguiStyle.ChildRounding = 3.0f;
    imguiStyle.FrameRounding = 3.0f;
    imguiStyle.GrabRounding = 3.0f;
    imguiStyle.PopupRounding = 3.0f;
    imguiStyle.ScrollbarRounding = 3.0f;
    imguiStyle.TabRounding = 3.0f;

    Theme thm = m_loadedThemes[name];

    uint8_t ret[4];
    std::map<std::string, std::string> params = thm.data;
    for (auto const& [param, val] : params) {
        if (param == "name") { continue; }

        if (param == "author") {
            m_themeAuthor = val;
            continue;
        }

        // If param is a color, check that it's a valid RGBA hex value
        const auto imguiColIter = sm_imguiColorStringToCodeTable.find(param);
        if (imguiColIter != sm_imguiColorStringToCodeTable.end()) {
            decodeRGBA(val, ret);
            imguiStyle.Colors[imguiColIter->second] = ImVec4((float)ret[0] / 255.0f, (float)ret[1] / 255.0f, (float)ret[2] / 255.0f, (float)ret[3] / 255.0f);
            continue;
        }

        const auto implotColIter = sm_implotColorStringToCodeTable.find(param);
        if (implotColIter != sm_implotColorStringToCodeTable.end()) {
            decodeRGBA(val, ret);
            implotStyle.Colors[implotColIter->second] = ImVec4((float)ret[0] / 255.0f, (float)ret[1] / 255.0f, (float)ret[2] / 255.0f, (float)ret[3] / 255.0f);
            continue;
        }

        const auto coreColIter = sm_coreColorStringToCodeTable.find(param);
        if (coreColIter != sm_coreColorStringToCodeTable.end()) {
            decodeRGBA(val, ret);
            m_colorArray[coreColIter->second] = ImVec4((float)ret[0] / 255.0f, (float)ret[1] / 255.0f, (float)ret[2] / 255.0f, (float)ret[3] / 255.0f);
            continue;
        }
    }

    return true;
}

bool ThemeManager::decodeRGBA(const std::string& str, uint8_t out[4]) {
    if (str[0] != '#' || !std::all_of(str.begin() + 1, str.end(), ::isxdigit) || str.length() != 9) {
        return false;
    }
    out[0] = std::stoi(str.substr(1, 2), NULL, 16);
    out[1] = std::stoi(str.substr(3, 2), NULL, 16);
    out[2] = std::stoi(str.substr(5, 2), NULL, 16);
    out[3] = std::stoi(str.substr(7, 2), NULL, 16);
    return true;
}

std::vector<std::string> ThemeManager::getThemeNames() {
    std::vector<std::string> names;
    for (auto [name, theme] : m_loadedThemes) { names.push_back(name); }
    return names;
}

const ImVec4& ThemeManager::getCoreColor(const CoreCol col) {
    return m_colorArray[col];
}

const std::unordered_map<std::string_view, int> ThemeManager::sm_coreColorStringToCodeTable = {
    { "WaterfallBackground", CoreCol_WaterfallBg },
    { "ClearColor", CoreCol_ClearColor },
    { "FFTHoldColor", CoreCol_FFTHoldColor },
    { "CrosshairColor", CoreCol_CrosshairColor },
    { "MainBorderColor", CoreCol_MainBorderColor },
    { "WaterfallSeparatorColor", CoreCol_WaterfallSeparatorColor },
};

const std::unordered_map<std::string_view, int> ThemeManager::sm_imguiColorStringToCodeTable = {
    { "Text", ImGuiCol_Text },
    { "TextDisabled", ImGuiCol_TextDisabled },
    { "WindowBg", ImGuiCol_WindowBg },
    { "ChildBg", ImGuiCol_ChildBg },
    { "PopupBg", ImGuiCol_PopupBg },
    { "Border", ImGuiCol_Border },
    { "BorderShadow", ImGuiCol_BorderShadow },
    { "FrameBg", ImGuiCol_FrameBg },
    { "FrameBgHovered", ImGuiCol_FrameBgHovered },
    { "FrameBgActive", ImGuiCol_FrameBgActive },
    { "TitleBg", ImGuiCol_TitleBg },
    { "TitleBgActive", ImGuiCol_TitleBgActive },
    { "TitleBgCollapsed", ImGuiCol_TitleBgCollapsed },
    { "MenuBarBg", ImGuiCol_MenuBarBg },
    { "ScrollbarBg", ImGuiCol_ScrollbarBg },
    { "ScrollbarGrab", ImGuiCol_ScrollbarGrab },
    { "ScrollbarGrabHovered", ImGuiCol_ScrollbarGrabHovered },
    { "ScrollbarGrabActive", ImGuiCol_ScrollbarGrabActive },
    { "CheckMark", ImGuiCol_CheckMark },
    { "SliderGrab", ImGuiCol_SliderGrab },
    { "SliderGrabActive", ImGuiCol_SliderGrabActive },
    { "Button", ImGuiCol_Button },
    { "ButtonHovered", ImGuiCol_ButtonHovered },
    { "ButtonActive", ImGuiCol_ButtonActive },
    { "Header", ImGuiCol_Header },
    { "HeaderHovered", ImGuiCol_HeaderHovered },
    { "HeaderActive", ImGuiCol_HeaderActive },
    { "Separator", ImGuiCol_Separator },
    { "SeparatorHovered", ImGuiCol_SeparatorHovered },
    { "SeparatorActive", ImGuiCol_SeparatorActive },
    { "ResizeGrip", ImGuiCol_ResizeGrip },
    { "ResizeGripHovered", ImGuiCol_ResizeGripHovered },
    { "ResizeGripActive", ImGuiCol_ResizeGripActive },
    { "InputTextCursor", ImGuiCol_InputTextCursor },
    { "TabHovered", ImGuiCol_TabHovered },
    { "Tab", ImGuiCol_Tab },
    { "TabSelected", ImGuiCol_TabSelected },
    { "TabSelectedOverline", ImGuiCol_TabSelectedOverline },
    { "TabDimmed", ImGuiCol_TabDimmed },
    { "TabDimmedSelected", ImGuiCol_TabDimmedSelected },
    { "TabDimmedSelectedOverline", ImGuiCol_TabDimmedSelectedOverline },
    { "DockingPreview", ImGuiCol_DockingPreview },
    { "DockingEmptyBg", ImGuiCol_DockingEmptyBg },
    { "PlotLines", ImGuiCol_PlotLines },
    { "PlotLinesHovered", ImGuiCol_PlotLinesHovered },
    { "PlotHistogram", ImGuiCol_PlotHistogram },
    { "PlotHistogramHovered", ImGuiCol_PlotHistogramHovered },
    { "TableHeaderBg", ImGuiCol_TableHeaderBg },
    { "TableBorderStrong", ImGuiCol_TableBorderStrong },
    { "TableBorderLight", ImGuiCol_TableBorderLight },
    { "TableRowBg", ImGuiCol_TableRowBg },
    { "TableRowBgAlt", ImGuiCol_TableRowBgAlt },
    { "TextLink", ImGuiCol_TextLink },
    { "TextSelectedBg", ImGuiCol_TextSelectedBg },
    { "TreeLines", ImGuiCol_TreeLines },
    { "DragDropTarget", ImGuiCol_DragDropTarget },
    { "DragDropTargetBg", ImGuiCol_DragDropTargetBg },
    { "UnsavedMarker", ImGuiCol_UnsavedMarker },
    { "NavCursor", ImGuiCol_NavCursor },
    { "NavWindowingHighlight", ImGuiCol_NavWindowingHighlight },
    { "NavWindowingDimBg", ImGuiCol_NavWindowingDimBg },
    { "ModalWindowDimBg", ImGuiCol_ModalWindowDimBg },
};

const std::unordered_map<std::string_view, int> ThemeManager::sm_implotColorStringToCodeTable = {
    { "PlotLine", ImPlotCol_Line },
    { "PlotFill", ImPlotCol_Fill },
    { "PlotMarkerOutline", ImPlotCol_MarkerOutline },
    { "PlotMarkerFill", ImPlotCol_MarkerFill },
    { "PlotErrorBar", ImPlotCol_ErrorBar },
    { "PlotFrameBg", ImPlotCol_FrameBg },
    { "PlotBg", ImPlotCol_PlotBg },
    { "PlotBorder", ImPlotCol_PlotBorder },
    { "PlotLegendBg", ImPlotCol_LegendBg },
    { "PlotLegendBorder", ImPlotCol_LegendBorder },
    { "PlotLegendText", ImPlotCol_LegendText },
    { "PlotTitleText", ImPlotCol_TitleText },
    { "PlotInlayText", ImPlotCol_InlayText },
    { "PlotAxisText", ImPlotCol_AxisText },
    { "PlotAxisGrid", ImPlotCol_AxisGrid },
    { "PlotAxisTick", ImPlotCol_AxisTick },
    { "PlotAxisBg", ImPlotCol_AxisBg },
    { "PlotAxisBgHovered", ImPlotCol_AxisBgHovered },
    { "PlotAxisBgActive", ImPlotCol_AxisBgActive },
    { "PlotSelection", ImPlotCol_Selection },
    { "PlotCrosshairs", ImPlotCol_Crosshairs },
};
