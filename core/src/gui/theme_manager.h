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
    bool loadThemesFromDir(const std::string& path);
    bool loadTheme(const std::string& path);
    bool applyTheme(const std::string& name);
    inline const std::string& getThemeAuthor() const { return m_themeAuthor; }

    std::vector<std::string> getThemeNames();

    ImVec4 waterfallBg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 clearColor = ImVec4(0.0666f, 0.0666f, 0.0666f, 1.0f);
    ImVec4 fftHoldColor = ImVec4(0.0f, 1.0f, 0.75f, 1.0f);
    ImVec4 crosshairColor = ImVec4(0.86f, 0.86f, 0.0f, 1.0f);

private:
    static bool decodeRGBA(const std::string& str, uint8_t out[4]);

    static const std::unordered_map<std::string_view, int> sm_imguiColorStringToCodeTable;
    static const std::unordered_map<std::string_view, int> sm_implotColorStringToCodeTable;

    std::map<std::string, Theme> m_loadedThemes;
    std::string m_themeAuthor;
};