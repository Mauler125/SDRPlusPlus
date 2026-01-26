#include <gui/widgets/menu.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <gui/style.h>

Menu::Menu() {
}

void Menu::registerEntry(const std::string& name, void (*drawHandler)(void* ctx), void* ctx, ModuleManager::Instance* inst) {
    MenuItem_t item;
    item.drawHandler = drawHandler;
    item.ctx = ctx;
    item.inst = inst;
    items[name] = item;
    if (!isInOrderList(name)) {
        MenuOption_t opt;
        opt.name = name;
        opt.open = true;
        order.push_back(opt);
    }
}

void Menu::removeEntry(const std::string& name) {
    items.erase(name);
}

bool Menu::draw(bool updateStates) {
    bool changed = false;
    float menuWidth = ImGui::GetContentRegionAvail().x;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();

    float frameHeight = ImGui::GetFrameHeight();
    float horizontalGap = style.ItemSpacing.x;

    int displayedCount = 0;
    int rawId = 0;

    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

    for (MenuOption_t& opt : order) {
        rawId++;
        if (items.find(opt.name) == items.end()) {
            continue;
        }

        if (canDragMenuItems) {
            if (opt.name == draggedMenuName) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(draggedMenuName.c_str(), &draggedMenuName[draggedMenuName.length()]);
                ImGui::EndTooltip();
                continue;
            }

            // Draw dragged menu item
            if (displayedCount == insertBefore && !draggedMenuName.empty()) {
                if (updateStates) { ImGui::SetNextItemOpen(false); }
                ImVec2 posMin = ImGui::GetCursorScreenPos();
                ImRect originalRect = window->WorkRect;

                float rightBoundary = window->Pos.x + ImGui::GetWindowContentRegionMax().x + (style.WindowPadding.x * 0.5f);

                if (items[draggedOpt.name].inst != NULL) {
                    window->WorkRect.Max.x = rightBoundary - (frameHeight + horizontalGap);
                }

                ImGui::BeginDisabled();
                ImGui::CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str());
                ImVec2 nextPos = ImGui::GetCursorScreenPos();
                window->WorkRect = originalRect;

                if (items[draggedOpt.name].inst != NULL) {
                    ImGui::SetCursorScreenPos(ImVec2(rightBoundary - frameHeight, posMin.y));
                    bool enabled = items[draggedOpt.name].inst->isEnabled();
                    ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
                    ImGui::SetCursorScreenPos(nextPos);
                }
                ImGui::EndDisabled();

                ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + frameHeight);
                window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::uiScale);
            }
        }

        displayedCount++;
        MenuItem_t& item = items[opt.name];

        ImVec2 posMin = ImGui::GetCursorScreenPos();
        headerTops[displayedCount - 1] = posMin.y;
        optionIDs[displayedCount - 1] = rawId - 1;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(posMin, ImVec2(posMin.x + menuWidth, posMin.y + frameHeight))) {
            menuClicked = true;
            clickedMenuName = opt.name;
        }

        if (canDragMenuItems) {
            const bool menuDragged = (menuClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && draggedMenuName.empty() && clickedMenuName == opt.name);
            if (menuDragged && !locked) {
                draggedMenuName = opt.name;
                draggedId = rawId - 1;
                draggedOpt = opt;
                continue;
            }
            else if (menuDragged) {
                ImGui::SetTooltip("Menu Order Locked!");
            }
        }

        // Draw menu header and checkbox
        if (updateStates) { ImGui::SetNextItemOpen(opt.open); }

        const ImRect originalRect = window->WorkRect;
        const float rightBoundary = window->Pos.x + ImGui::GetWindowContentRegionMax().x + (style.WindowPadding.x * 0.5f);

        if (item.inst != NULL) {
            window->WorkRect.Max.x = rightBoundary - (frameHeight + horizontalGap);
        }

        const bool isHeaderOpen = ImGui::CollapsingHeader((opt.name + "##sdrpp_main_menu").c_str());
        const ImVec2 nextItemPos = ImGui::GetCursorScreenPos();

        window->WorkRect = originalRect;

        if (item.inst != NULL) {
            ImGui::SetCursorScreenPos(ImVec2(rightBoundary - frameHeight, posMin.y));
            bool enabled = item.inst->isEnabled();
            if (ImGui::Checkbox(("##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                enabled ? item.inst->enable() : item.inst->disable();
                changed = true;
            }
            // Restore cursor
            ImGui::SetCursorScreenPos(nextItemPos);
        }

        if (isHeaderOpen) {
            if (!opt.open && !updateStates) {
                opt.open = true;
                changed = true;
            }
            item.drawHandler(item.ctx);
            ImGui::Spacing();
        }
        else {
            if (opt.open && !updateStates) {
                opt.open = false;
                changed = true;
            }
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && menuClicked) {

        if (!draggedMenuName.empty()) {
            // Move menu
            order.erase(order.begin() + draggedId);

            if (insertBefore == displayedCount) {
                order.push_back(draggedOpt);
            }
            else if (!insertBeforeName.empty()) {
                size_t beforeId = 0;
                for (size_t i = 0; i < order.size(); i++) {
                    if (order[i].name == insertBeforeName) {
                        beforeId = i;
                        break;
                    }
                }
                order.insert(order.begin() + beforeId, draggedOpt);
            }
            changed = true;
        }

        menuClicked = false;
        draggedMenuName.clear();
        insertBeforeName.clear();
        insertBefore = -1;
    }

    // TODO: Figure out why the hell this is needed
    if (insertBefore == displayedCount && !draggedMenuName.empty()) {
        if (updateStates) { ImGui::SetNextItemOpen(false); }
        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImRect originalRect = window->WorkRect;
        float rightBoundary = window->Pos.x + ImGui::GetWindowContentRegionMax().x + (style.WindowPadding.x * 0.5f);

        if (items[draggedOpt.name].inst != NULL) {
            window->WorkRect.Max.x = rightBoundary - (frameHeight + horizontalGap);
        }

        ImGui::BeginDisabled();
        ImGui::CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str());
        ImVec2 nextPos = ImGui::GetCursorScreenPos();
        window->WorkRect = originalRect;

        if (items[draggedOpt.name].inst != NULL) {
            ImGui::SetCursorScreenPos(ImVec2(rightBoundary - frameHeight, posMin.y));
            bool enabled = items[draggedOpt.name].inst->isEnabled();
            ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
            ImGui::SetCursorScreenPos(nextPos);
        }
        ImGui::EndDisabled();
        window->DrawList->AddRect(posMin, ImVec2(posMin.x + menuWidth, posMin.y + frameHeight), textColor, 0.0f, 0, style::uiScale);
    }

    if (!draggedMenuName.empty()) {
        insertBefore = displayedCount;
        ImVec2 mPos = ImGui::GetMousePos();
        for (int i = 0; i < displayedCount; i++) {
            if (headerTops[i] > mPos.y) {
                insertBefore = i;
                insertBeforeName = order[optionIDs[i]].name;
                break;
            }
        }
    }

    return changed;
}

bool Menu::isInOrderList(const std::string& name) {
    for (MenuOption_t opt : order) {
        if (opt.name == name) {
            return true;
        }
    }
    return false;
}
