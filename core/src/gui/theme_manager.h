#pragma once
#include <map>
#include <unordered_map>
#include <mutex>
#include <imgui.h>
#include <vector>
#include <json.hpp>
#include <string_view>

using nlohmann::json;

struct Theme {
    std::string author;
    json data;
};

class ThemeManager {
public:
    ThemeManager();
    void initCoreColors();

    bool loadThemesFromDir(const std::string& path);
    bool loadTheme(const std::string& path);
    bool applyTheme(const std::string& name);
    inline const std::string& getThemeAuthor() const { return m_themeAuthor; }

    std::vector<std::string> getThemeNames();

    enum CoreCol {
        CoreCol_WaterfallBg,
        CoreCol_ClearColor,
        CoreCol_FFTHoldColor,
        CoreCol_CrosshairColor,
        CoreCol_MainBorderColor,
        CoreCol_WaterfallSeparatorColor,

        CoreCol_COUNT
    };

    const ImVec4& getCoreColor(const CoreCol col);

private:
    static bool decodeRGBA(const std::string& str, uint8_t out[4]);

    static const std::unordered_map<std::string_view, int> sm_coreColorStringToCodeTable;
    static const std::unordered_map<std::string_view, int> sm_imguiColorStringToCodeTable;
    static const std::unordered_map<std::string_view, int> sm_implotColorStringToCodeTable;
    static const std::unordered_map<std::string_view, int> sm_imguiVarStringToCodeTable;
    static const std::unordered_map<std::string_view, int> sm_implotVarStringToCodeTable;

    std::map<std::string, Theme> m_loadedThemes;
    std::string m_themeAuthor;

    ImVec4 m_colorArray[CoreCol_COUNT];
};