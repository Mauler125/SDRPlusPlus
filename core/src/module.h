#pragma once
#include <string>
#include <map>
#include <json.hpp>
#include <utils/event.h>

#ifdef _WIN32
#ifdef SDRPP_IS_CORE
#define SDRPP_EXPORT extern "C" __declspec(dllexport)
#else
#define SDRPP_EXPORT extern "C" __declspec(dllimport)
#endif
#else
#define SDRPP_EXPORT extern
#endif

#ifdef _WIN32
#include <Windows.h>
#define MOD_EXPORT           extern "C" __declspec(dllexport)
#define SDRPP_MOD_EXTENTSION ".i64"
#else
#include <dlfcn.h>
#define MOD_EXPORT extern "C"
#ifdef __APPLE__
#define SDRPP_MOD_EXTENTSION ".dylib"
#else
#define SDRPP_MOD_EXTENTSION ".so"
#endif
#endif

class ModuleManager {
public:
    struct ModuleInfo_t {
        const char* name;
        const char* description;
        const char* author;
        const int versionMajor;
        const int versionMinor;
        const int versionBuild;
        const int maxInstances;
    };

    class Instance {
    public:
        virtual ~Instance() {}
        virtual void postInit() = 0;
        virtual void enable() = 0;
        virtual void disable() = 0;
        virtual bool isEnabled() = 0;
    };

    struct Module_t {
#ifdef _WIN32
        HMODULE handle;
#else
        void* handle;
#endif
        ModuleManager::ModuleInfo_t* info;
        void (*init)();
        ModuleManager::Instance* (*createInstance)(const std::string& name);
        void (*deleteInstance)(ModuleManager::Instance* const instance);
        void (*end)();

        friend bool operator==(const Module_t& a, const Module_t& b) {
            if (a.handle != b.handle) { return false; }
            if (a.info != b.info) { return false; }
            if (a.init != b.init) { return false; }
            if (a.createInstance != b.createInstance) { return false; }
            if (a.deleteInstance != b.deleteInstance) { return false; }
            if (a.end != b.end) { return false; }
            return true;
        }
    };

    struct Instance_t {
        ModuleManager::Module_t module;
        ModuleManager::Instance* instance;
    };

    ModuleManager::Module_t loadModule(const std::string& path);

    int createInstance(const std::string& name, const std::string& module);
    int deleteInstance(const std::string& name);
    int deleteInstance(ModuleManager::Instance* instance);

    int enableInstance(const std::string& name);
    int disableInstance(const std::string& name);
    bool instanceEnabled(const std::string& name);
    void postInit(const std::string& name);
    std::string getInstanceModuleName(const std::string& name);

    int countModuleInstances(const std::string& module);

    void doPostInitAll();
    void doShutdownAll();

    Event<const std::string&> onInstanceCreated;
    Event<const std::string&> onInstanceDelete;
    Event<const std::string&> onInstanceDeleted;

    std::map<std::string, ModuleManager::Module_t> modules;
    std::map<std::string, ModuleManager::Instance_t> instances;
};

#define SDRPP_MOD_INFO MOD_EXPORT const ModuleManager::ModuleInfo_t _INFO_