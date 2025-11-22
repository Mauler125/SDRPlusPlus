#pragma once
#include <thread>
#include <string>
#include <mutex>

namespace LoadingScreen {
    void init();
    void show(const std::string& msg);
};