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

static bool IsValidColorString(const json& val) {
    if (!val.is_string()) return false;
    std::string s = val.get<std::string>();
    return (s.length() == 9 && s[0] == '#' && std::all_of(s.begin() + 1, s.end(), ::isxdigit));
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
    try {
        file >> data;
    }
    catch (std::exception& e) {
        flog::error("Failed to parse JSON in theme {0}: {1}", path, e.what());
        return false;
    }
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
    for (const auto& [param, val] : data.items()) {
        if (param == "name" || param == "author") { continue; }

        bool isValid = false;

        // If param is a color, check that it's a valid RGBA hex value
        if (sm_imguiColorStringToCodeTable.find(param) != sm_imguiColorStringToCodeTable.end() ||
            sm_implotColorStringToCodeTable.find(param) != sm_implotColorStringToCodeTable.end() ||
            sm_coreColorStringToCodeTable.find(param) != sm_coreColorStringToCodeTable.end()) {

            if (!IsValidColorString(val)) {
                flog::error("Theme {0} contains invalid {1} field. Expected hex RGBA color string (#RRGGBBAA)", path, param);
                return false;
            }
            isValid = true;
        }
        // Check ImGui Style Vars
        else if (sm_imguiVarStringToCodeTable.find(param) != sm_imguiVarStringToCodeTable.end()) {
            if (!val.is_number() && !val.is_array()) {
                flog::error("Theme {0} contains invalid {1} field. Expected number or array.", path, param);
                return false;
            }
            isValid = true;
        }
        // Check ImPlot Style Vars
        else if (sm_implotVarStringToCodeTable.find(param) != sm_implotVarStringToCodeTable.end()) {
            if (!val.is_number() && !val.is_array()) {
                flog::error("Theme {0} contains invalid {1} field. Expected number or array.", path, param);
                return false;
            }
            isValid = true;
        }

        if (!isValid) {
            flog::error("Theme {0} contains unknown field: {1}", path, param);
            return false;
        }
    }

    thm.data = data;
    m_loadedThemes[name] = thm;

    return true;
}

static ImVec2 JsonToVec2(const json& val) {
    if (val.is_array() && val.size() >= 2) {
        return ImVec2(val[0].get<float>(), val[1].get<float>());
    }
    if (val.is_number()) {
        const float f = val.get<float>();
        return ImVec2(f, f);
    }
    return ImVec2(0, 0);
}

bool ThemeManager::applyTheme(const std::string& name) {
    if (m_loadedThemes.find(name) == m_loadedThemes.end()) {
        flog::error("Unknown theme: {0}", name);
        return false;
    }

    ImGui::StyleColorsClassic();
    ImPlot::StyleColorsClassic();
    initCoreColors();

    auto& imguiStyle = ImGui::GetStyle();
    auto& implotStyle = ImPlot::GetStyle();

    imguiStyle.WindowPadding = ImVec2(8, 8);
    imguiStyle.FramePadding = ImVec2(4, 3);
    imguiStyle.ItemSpacing = ImVec2(8, 4);
    imguiStyle.ItemInnerSpacing = ImVec2(4, 4);
    imguiStyle.WindowRounding = 6.0f;
    imguiStyle.ChildRounding = 3.0f;
    imguiStyle.FrameRounding = 3.0f;
    imguiStyle.GrabRounding = 3.0f;
    imguiStyle.PopupRounding = 3.0f;
    imguiStyle.ScrollbarRounding = 3.0f;
    imguiStyle.TabRounding = 3.0f;

    Theme& thm = m_loadedThemes[name];
    uint8_t colRet[4];

    for (const auto& [param, val] : thm.data.items()) {
        if (param == "name") { continue; }
        if (param == "author") {
            if (val.is_string()) { m_themeAuthor = val.get<std::string>(); }
            continue;
        }

        // If param is a color, check that it's a valid RGBA hex value
        const auto imguiColIter = sm_imguiColorStringToCodeTable.find(param);
        if (imguiColIter != sm_imguiColorStringToCodeTable.end()) {
            if (decodeRGBA(val.get<std::string>(), colRet)) {
                imguiStyle.Colors[imguiColIter->second] = ImVec4((float)colRet[0] / 255.0f, (float)colRet[1] / 255.0f, (float)colRet[2] / 255.0f, (float)colRet[3] / 255.0f);
            }
            continue;
        }

        const auto implotColIter = sm_implotColorStringToCodeTable.find(param);
        if (implotColIter != sm_implotColorStringToCodeTable.end()) {
            if (decodeRGBA(val.get<std::string>(), colRet)) {
                implotStyle.Colors[implotColIter->second] = ImVec4((float)colRet[0] / 255.0f, (float)colRet[1] / 255.0f, (float)colRet[2] / 255.0f, (float)colRet[3] / 255.0f);
            }
            continue;
        }

        const auto coreColIter = sm_coreColorStringToCodeTable.find(param);
        if (coreColIter != sm_coreColorStringToCodeTable.end()) {
            if (decodeRGBA(val.get<std::string>(), colRet)) {
                m_colorArray[coreColIter->second] = ImVec4((float)colRet[0] / 255.0f, (float)colRet[1] / 255.0f, (float)colRet[2] / 255.0f, (float)colRet[3] / 255.0f);
            }
            continue;
        }

        const auto imguiVarIter = sm_imguiVarStringToCodeTable.find(param);
        if (imguiVarIter != sm_imguiVarStringToCodeTable.end()) {
            ImGuiStyleVar_ varId = (ImGuiStyleVar_)imguiVarIter->second;
            switch (varId) {
            case ImGuiStyleVar_Alpha:
                imguiStyle.Alpha = val.get<float>();
                break;
            case ImGuiStyleVar_DisabledAlpha:
                imguiStyle.DisabledAlpha = val.get<float>();
                break;
            case ImGuiStyleVar_WindowPadding:
                imguiStyle.WindowPadding = JsonToVec2(val);
                break;
            case ImGuiStyleVar_WindowRounding:
                imguiStyle.WindowRounding = val.get<float>();
                break;
            case ImGuiStyleVar_WindowBorderSize:
                imguiStyle.WindowBorderSize = val.get<float>();
                break;
            case ImGuiStyleVar_WindowMinSize:
                imguiStyle.WindowMinSize = JsonToVec2(val);
                break;
            case ImGuiStyleVar_WindowTitleAlign:
                imguiStyle.WindowTitleAlign = JsonToVec2(val);
                break;
            case ImGuiStyleVar_ChildRounding:
                imguiStyle.ChildRounding = val.get<float>();
                break;
            case ImGuiStyleVar_ChildBorderSize:
                imguiStyle.ChildBorderSize = val.get<float>();
                break;
            case ImGuiStyleVar_PopupRounding:
                imguiStyle.PopupRounding = val.get<float>();
                break;
            case ImGuiStyleVar_PopupBorderSize:
                imguiStyle.PopupBorderSize = val.get<float>();
                break;
            case ImGuiStyleVar_FramePadding:
                imguiStyle.FramePadding = JsonToVec2(val);
                break;
            case ImGuiStyleVar_FrameRounding:
                imguiStyle.FrameRounding = val.get<float>();
                break;
            case ImGuiStyleVar_FrameBorderSize:
                imguiStyle.FrameBorderSize = val.get<float>();
                break;
            case ImGuiStyleVar_ItemSpacing:
                imguiStyle.ItemSpacing = JsonToVec2(val);
                break;
            case ImGuiStyleVar_ItemInnerSpacing:
                imguiStyle.ItemInnerSpacing = JsonToVec2(val);
                break;
            case ImGuiStyleVar_IndentSpacing:
                imguiStyle.IndentSpacing = val.get<float>();
                break;
            case ImGuiStyleVar_CellPadding:
                imguiStyle.CellPadding = JsonToVec2(val);
                break;
            case ImGuiStyleVar_ScrollbarSize:
                imguiStyle.ScrollbarSize = val.get<float>();
                break;
            case ImGuiStyleVar_ScrollbarRounding:
                imguiStyle.ScrollbarRounding = val.get<float>();
                break;
            case ImGuiStyleVar_ScrollbarPadding:
                imguiStyle.ScrollbarPadding = val.get<float>();
                break;
            case ImGuiStyleVar_GrabMinSize:
                imguiStyle.GrabMinSize = val.get<float>();
                break;
            case ImGuiStyleVar_GrabRounding:
                imguiStyle.GrabRounding = val.get<float>();
                break;
            case ImGuiStyleVar_ImageBorderSize:
                imguiStyle.ImageBorderSize = val.get<float>();
                break;
            case ImGuiStyleVar_TabRounding:
                imguiStyle.TabRounding = val.get<float>();
                break;
            case ImGuiStyleVar_TabBorderSize:
                imguiStyle.TabBorderSize = val.get<float>();
                break;
            case ImGuiStyleVar_TabMinWidthBase:
                imguiStyle.TabMinWidthBase = val.get<float>();
                break;
            case ImGuiStyleVar_TabMinWidthShrink:
                imguiStyle.TabMinWidthShrink = val.get<float>();
                break;
            case ImGuiStyleVar_TabBarBorderSize:
                imguiStyle.TabBarBorderSize = val.get<float>();
                break;
            case ImGuiStyleVar_TabBarOverlineSize:
                imguiStyle.TabBarOverlineSize = val.get<float>();
                break;
            case ImGuiStyleVar_TableAngledHeadersAngle:
                imguiStyle.TableAngledHeadersAngle = val.get<float>();
                break;
            case ImGuiStyleVar_TableAngledHeadersTextAlign:
                imguiStyle.TableAngledHeadersTextAlign = JsonToVec2(val);
                break;
            case ImGuiStyleVar_TreeLinesSize:
                imguiStyle.TreeLinesSize = val.get<float>();
                break;
            case ImGuiStyleVar_TreeLinesRounding:
                imguiStyle.TreeLinesRounding = val.get<float>();
                break;
            case ImGuiStyleVar_ButtonTextAlign:
                imguiStyle.ButtonTextAlign = JsonToVec2(val);
                break;
            case ImGuiStyleVar_SelectableTextAlign:
                imguiStyle.SelectableTextAlign = JsonToVec2(val);
                break;
            case ImGuiStyleVar_SeparatorTextBorderSize:
                imguiStyle.SeparatorTextBorderSize = val.get<float>();
                break;
            case ImGuiStyleVar_SeparatorTextAlign:
                imguiStyle.SeparatorTextAlign = JsonToVec2(val);
                break;
            case ImGuiStyleVar_SeparatorTextPadding:
                imguiStyle.SeparatorTextPadding = JsonToVec2(val);
                break;
            case ImGuiStyleVar_DockingSeparatorSize:
                imguiStyle.DockingSeparatorSize = val.get<float>();
                break;
            default:
                break;
            }
            continue;
        }

        const auto implotVarIter = sm_implotVarStringToCodeTable.find(param);
        if (implotVarIter != sm_implotVarStringToCodeTable.end()) {
            ImPlotStyleVar_ varId = (ImPlotStyleVar_)implotVarIter->second;
            switch (varId) {
            case ImPlotStyleVar_LineWeight:
                implotStyle.LineWeight = val.get<float>();
                break;
            case ImPlotStyleVar_Marker:
                implotStyle.Marker = val.get<int>();
                break;
            case ImPlotStyleVar_MarkerSize:
                implotStyle.MarkerSize = val.get<float>();
                break;
            case ImPlotStyleVar_MarkerWeight:
                implotStyle.MarkerWeight = val.get<float>();
                break;
            case ImPlotStyleVar_FillAlpha:
                implotStyle.FillAlpha = val.get<float>();
                break;
            case ImPlotStyleVar_ErrorBarSize:
                implotStyle.ErrorBarSize = val.get<float>();
                break;
            case ImPlotStyleVar_ErrorBarWeight:
                implotStyle.ErrorBarWeight = val.get<float>();
                break;
            case ImPlotStyleVar_DigitalBitHeight:
                implotStyle.DigitalBitHeight = val.get<float>();
                break;
            case ImPlotStyleVar_DigitalBitGap:
                implotStyle.DigitalBitGap = val.get<float>();
                break;
            case ImPlotStyleVar_PlotBorderSize:
                implotStyle.PlotBorderSize = val.get<float>();
                break;
            case ImPlotStyleVar_MinorAlpha:
                implotStyle.MinorAlpha = val.get<float>();
                break;
            case ImPlotStyleVar_MajorTickLen:
                implotStyle.MajorTickLen = JsonToVec2(val);
                break;
            case ImPlotStyleVar_MinorTickLen:
                implotStyle.MinorTickLen = JsonToVec2(val);
                break;
            case ImPlotStyleVar_MajorTickSize:
                implotStyle.MajorTickSize = JsonToVec2(val);
                break;
            case ImPlotStyleVar_MinorTickSize:
                implotStyle.MinorTickSize = JsonToVec2(val);
                break;
            case ImPlotStyleVar_MajorGridSize:
                implotStyle.MajorGridSize = JsonToVec2(val);
                break;
            case ImPlotStyleVar_MinorGridSize:
                implotStyle.MinorGridSize = JsonToVec2(val);
                break;
            case ImPlotStyleVar_PlotPadding:
                implotStyle.PlotPadding = JsonToVec2(val);
                break;
            case ImPlotStyleVar_LabelPadding:
                implotStyle.LabelPadding = JsonToVec2(val);
                break;
            case ImPlotStyleVar_LegendPadding:
                implotStyle.LegendPadding = JsonToVec2(val);
                break;
            case ImPlotStyleVar_LegendInnerPadding:
                implotStyle.LegendInnerPadding = JsonToVec2(val);
                break;
            case ImPlotStyleVar_LegendSpacing:
                implotStyle.LegendSpacing = JsonToVec2(val);
                break;
            case ImPlotStyleVar_MousePosPadding:
                implotStyle.MousePosPadding = JsonToVec2(val);
                break;
            case ImPlotStyleVar_AnnotationPadding:
                implotStyle.AnnotationPadding = JsonToVec2(val);
                break;
            case ImPlotStyleVar_FitPadding:
                implotStyle.FitPadding = JsonToVec2(val);
                break;
            case ImPlotStyleVar_PlotDefaultSize:
                implotStyle.PlotDefaultSize = JsonToVec2(val);
                break;
            case ImPlotStyleVar_PlotMinSize:
                implotStyle.PlotMinSize = JsonToVec2(val);
                break;
            default:
                break;
            }
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

const std::unordered_map<std::string_view, int> ThemeManager::sm_imguiVarStringToCodeTable = {
    { "Alpha", ImGuiStyleVar_Alpha },
    { "DisabledAlpha", ImGuiStyleVar_DisabledAlpha },
    { "WindowPadding", ImGuiStyleVar_WindowPadding },
    { "WindowRounding", ImGuiStyleVar_WindowRounding },
    { "WindowBorderSize", ImGuiStyleVar_WindowBorderSize },
    { "WindowMinSize", ImGuiStyleVar_WindowMinSize },
    { "WindowTitleAlign", ImGuiStyleVar_WindowTitleAlign },
    { "ChildRounding", ImGuiStyleVar_ChildRounding },
    { "ChildBorderSize", ImGuiStyleVar_ChildBorderSize },
    { "PopupRounding", ImGuiStyleVar_PopupRounding },
    { "PopupBorderSize", ImGuiStyleVar_PopupBorderSize },
    { "FramePadding", ImGuiStyleVar_FramePadding },
    { "FrameRounding", ImGuiStyleVar_FrameRounding },
    { "FrameBorderSize", ImGuiStyleVar_FrameBorderSize },
    { "ItemSpacing", ImGuiStyleVar_ItemSpacing },
    { "ItemInnerSpacing", ImGuiStyleVar_ItemInnerSpacing },
    { "IndentSpacing", ImGuiStyleVar_IndentSpacing },
    { "CellPadding", ImGuiStyleVar_CellPadding },
    { "ScrollbarSize", ImGuiStyleVar_ScrollbarSize },
    { "ScrollbarRounding", ImGuiStyleVar_ScrollbarRounding },
    { "ScrollbarPadding", ImGuiStyleVar_ScrollbarPadding },
    { "GrabMinSize", ImGuiStyleVar_GrabMinSize },
    { "GrabRounding", ImGuiStyleVar_GrabRounding },
    { "ImageBorderSize", ImGuiStyleVar_ImageBorderSize },
    { "TabRounding", ImGuiStyleVar_TabRounding },
    { "TabBorderSize", ImGuiStyleVar_TabBorderSize },
    { "TabMinWidthBase", ImGuiStyleVar_TabMinWidthBase },
    { "TabMinWidthShrink", ImGuiStyleVar_TabMinWidthShrink },
    { "TabBarBorderSize", ImGuiStyleVar_TabBarBorderSize },
    { "TabBarOverlineSize", ImGuiStyleVar_TabBarOverlineSize },
    { "TableAngledHeadersAngle", ImGuiStyleVar_TableAngledHeadersAngle },
    { "TableAngledHeadersTextAlign", ImGuiStyleVar_TableAngledHeadersTextAlign },
    { "TreeLinesSize", ImGuiStyleVar_TreeLinesSize },
    { "TreeLinesRounding", ImGuiStyleVar_TreeLinesRounding },
    { "ButtonTextAlign", ImGuiStyleVar_ButtonTextAlign },
    { "SelectableTextAlign", ImGuiStyleVar_SelectableTextAlign },
    { "SeparatorTextBorderSize", ImGuiStyleVar_SeparatorTextBorderSize },
    { "SeparatorTextAlign", ImGuiStyleVar_SeparatorTextAlign },
    { "SeparatorTextPadding", ImGuiStyleVar_SeparatorTextPadding },
    { "DockingSeparatorSize", ImGuiStyleVar_DockingSeparatorSize },
};

const std::unordered_map<std::string_view, int> ThemeManager::sm_implotVarStringToCodeTable = {
    { "PlotLineWeight", ImPlotStyleVar_LineWeight },
    { "PlotMarker", ImPlotStyleVar_Marker },
    { "PlotMarkerSize", ImPlotStyleVar_MarkerSize },
    { "PlotMarkerWeight", ImPlotStyleVar_MarkerWeight },
    { "PlotFillAlpha", ImPlotStyleVar_FillAlpha },
    { "PlotErrorBarSize", ImPlotStyleVar_ErrorBarSize },
    { "PlotErrorBarWeight", ImPlotStyleVar_ErrorBarWeight },
    { "PlotDigitalBitHeight", ImPlotStyleVar_DigitalBitHeight },
    { "PlotDigitalBitGap", ImPlotStyleVar_DigitalBitGap },
    { "PlotBorderSize", ImPlotStyleVar_PlotBorderSize },
    { "PlotMinorAlpha", ImPlotStyleVar_MinorAlpha },
    { "PlotMajorTickLen", ImPlotStyleVar_MajorTickLen },
    { "PlotMinorTickLen", ImPlotStyleVar_MinorTickLen },
    { "PlotMajorTickSize", ImPlotStyleVar_MajorTickSize },
    { "PlotMinorTickSize", ImPlotStyleVar_MinorTickSize },
    { "PlotMajorGridSize", ImPlotStyleVar_MajorGridSize },
    { "PlotMinorGridSize", ImPlotStyleVar_MinorGridSize },
    { "PlotPadding", ImPlotStyleVar_PlotPadding },
    { "PlotLabelPadding", ImPlotStyleVar_LabelPadding },
    { "PlotLegendPadding", ImPlotStyleVar_LegendPadding },
    { "PlotLegendInnerPadding", ImPlotStyleVar_LegendInnerPadding },
    { "PlotLegendSpacing", ImPlotStyleVar_LegendSpacing },
    { "PlotMousePosPadding", ImPlotStyleVar_MousePosPadding },
    { "PlotAnnotationPadding", ImPlotStyleVar_AnnotationPadding },
    { "PlotFitPadding", ImPlotStyleVar_FitPadding },
    { "PlotDefaultSize", ImPlotStyleVar_PlotDefaultSize },
    { "PlotMinSize", ImPlotStyleVar_PlotMinSize },
};
