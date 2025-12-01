#pragma once
#include <map>
#include <string>
#include <mutex>

struct ModuleComInterface {
    std::string moduleName;
    void* ctx;
    void (*handler)(int code, void* in, void* out, void* ctx);
};

class ModuleComManager {
public:
    bool registerInterface(const std::string& moduleName, const std::string& name, void (*handler)(int code, void* in, void* out, void* ctx), void* ctx);
    bool unregisterInterface(const std::string& name);
    bool interfaceExists(const std::string& name);
    std::string getModuleName(const std::string& name);
    bool callInterface(const std::string& name, int code, void* in, void* out);

private:
    std::recursive_mutex mtx;
    std::map<std::string, ModuleComInterface> interfaces;
};