#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

class FileSelect {
public:
    FileSelect(const std::string& defaultPath, std::vector<std::string> filter = { "All Files", "*" });
    bool render(const std::string& id);
    void setPath(const std::string& path, bool markChanged = false);
    bool pathIsValid();

    std::string expandString(const std::string& input) const;

    std::string path = "";


private:
    void worker();
    std::thread workerThread;
    std::vector<std::string> _filter;
    std::string root = "";

    bool pathValid = false;
    bool dialogOpen = false;
    bool pathChanged = false;
    char strPath[2048];
};