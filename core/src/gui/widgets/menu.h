#pragma once
#include <string>
#include <vector>
#include <map>
#include <module.h>

#define MAX_MENU_COUNT 1024

class Menu {
public:
    Menu();

    struct MenuOption_t {
        std::string name;
        bool open;
    };

    struct MenuItem_t {
        void (*drawHandler)(void* ctx);
        void* ctx;
        ModuleManager::Instance* inst;
    };

    void registerEntry(std::string name, void (*drawHandler)(void* ctx), void* ctx = NULL, ModuleManager::Instance* inst = NULL);
    void removeEntry(std::string name);
    bool draw(bool updateStates);

    std::vector<MenuOption_t> order;

    // Note: locked gives a tooltip message when trying to
    // order elements to tell you that you disabled it. If
    // we render windows above our main window, we don't
    // want the tool tips either, so canDragMenuItems has
    // to be unset in those cases to fully block all of it.
    bool locked = false;
    bool canDragMenuItems = true;

private:
    bool isInOrderList(std::string name);

    
    bool menuClicked = false;
    std::string clickedMenuName = "";
    std::string draggedMenuName = "";
    int insertBefore = -1;
    std::string insertBeforeName = "";

    int draggedId = 0;
    MenuOption_t draggedOpt;

    std::map<std::string, MenuItem_t> items;

    float headerTops[MAX_MENU_COUNT];
    int optionIDs[MAX_MENU_COUNT];
};