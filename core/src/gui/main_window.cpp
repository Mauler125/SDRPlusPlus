#include <gui/main_window.h>
#include <gui/gui.h>
#include "imgui.h"
#include <stdio.h>
#include <thread>
#include <complex>
#include <gui/widgets/waterfall.h>
#include <gui/widgets/frequency_select.h>
#include <signal_path/iq_frontend.h>
#include <gui/icons.h>
#include <gui/widgets/bandplan.h>
#include <gui/style.h>
#include <config.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <backend.h>
#include <gui/menus/source.h>
#include <gui/menus/display.h>
#include <gui/menus/bandplan.h>
#include <gui/menus/sink.h>
#include <gui/menus/vfo_color.h>
#include <gui/menus/module_manager.h>
#include <gui/menus/theme.h>
#include <gui/dialogs/credits.h>
#include <filesystem>
#include <signal_path/source.h>
#include <gui/dialogs/loading_screen.h>
#include <gui/colormaps.h>
#include <gui/widgets/snr_meter.h>
#include <gui/tuner.h>
#include <implot/implot.h>

void MainWindow::init() {
    LoadingScreen::show("Initializing UI");
    core::configManager.acquire();
    json menuElements = core::configManager.conf["menuElements"];
    std::string modulesDir = core::configManager.conf["modulesDirectory"];
    std::string resourcesDir = core::configManager.conf["resourcesDirectory"];
    core::configManager.release();

    // Assert that directories are absolute
    modulesDir = std::filesystem::absolute(modulesDir).string();
    resourcesDir = std::filesystem::absolute(resourcesDir).string();

    gui::waterfall.init(resourcesDir);
    gui::waterfall.setRawFFTSize(fftSize);

    credits::init();

    // Load menu elements
    gui::menu.order.clear();
    for (auto& elem : menuElements) {
        if (!elem.contains("name")) {
            flog::error("Menu element is missing name key");
            continue;
        }
        if (!elem["name"].is_string()) {
            flog::error("Menu element name isn't a string");
            continue;
        }
        if (!elem.contains("open")) {
            flog::error("Menu element is missing open key");
            continue;
        }
        if (!elem["open"].is_boolean()) {
            flog::error("Menu element name isn't a string");
            continue;
        }
        Menu::MenuOption_t opt;
        opt.name = elem["name"];
        opt.open = elem["open"];
        gui::menu.order.push_back(opt);
    }

    gui::menu.registerEntry("Source", sourcemenu::draw, NULL);
    gui::menu.registerEntry("Sinks", sinkmenu::draw, NULL);
    gui::menu.registerEntry("Band Plan", bandplanmenu::draw, NULL);
    gui::menu.registerEntry("Display", displaymenu::draw, NULL);
    gui::menu.registerEntry("VFO Color", vfo_color_menu::draw, NULL);
    gui::menu.registerEntry("Module Manager", module_manager_menu::draw, NULL);

    gui::freqSelect.init();

    // Set default values for waterfall in case no source init's it
    gui::waterfall.setBandwidth(8000000);
    gui::waterfall.setViewBandwidth(8000000);

    fft_in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fftwPlan = fftwf_plan_dft_1d(fftSize, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);

    sigpath::iqFrontEnd.init(&dummyStream, 8000000, true, 1, false, 1024, 20.0, IQFrontEnd::FFTWindow::NUTTALL, acquireFFTBuffer, releaseFFTBuffer, this);
    sigpath::iqFrontEnd.start();

    vfoCreatedHandler.handler = vfoAddedHandler;
    vfoCreatedHandler.ctx = this;
    sigpath::vfoManager.onVfoCreated.bindHandler(&vfoCreatedHandler);

    flog::info("Loading modules");

    // Load modules from /module directory
    if (std::filesystem::is_directory(modulesDir)) {
        for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
            std::string path = file.path().generic_string();
            if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            flog::info("Loading {0}", path);
            LoadingScreen::show("Loading " + file.path().filename().string());
            core::moduleManager.loadModule(path);
        }
    }
    else {
        flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    // Read module config
    core::configManager.acquire();
    std::vector<std::string> modules = core::configManager.conf["modules"];
    auto modList = core::configManager.conf["moduleInstances"].items();
    core::configManager.release();

    // Load additional modules specified through config
    for (auto const& path : modules) {
#ifndef __ANDROID__
        std::string apath = std::filesystem::absolute(path).string();
        flog::info("Loading {0}", apath);
        LoadingScreen::show("Loading " + std::filesystem::path(path).filename().string());
        core::moduleManager.loadModule(apath);
#else
        core::moduleManager.loadModule(path);
#endif
    }

    // Create module instances
    for (auto const& [name, _module] : modList) {
        std::string mod = _module["module"];
        bool enabled = _module["enabled"];
        flog::info("Initializing {0} ({1})", name, mod);
        LoadingScreen::show("Initializing " + name + " (" + mod + ")");
        core::moduleManager.createInstance(name, mod);
        if (!enabled) { core::moduleManager.disableInstance(name); }
    }

    // Load color maps
    LoadingScreen::show("Loading color maps");
    flog::info("Loading color maps");
    if (std::filesystem::is_directory(resourcesDir + "/colormaps")) {
        for (const auto& file : std::filesystem::directory_iterator(resourcesDir + "/colormaps")) {
            std::string path = file.path().generic_string();
            LoadingScreen::show("Loading " + file.path().filename().string());
            flog::info("Loading {0}", path);
            if (file.path().extension().generic_string() != ".json") {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            colormaps::loadMap(path);
        }
    }
    else {
        flog::warn("Color map directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    gui::waterfall.updatePallette(colormaps::maps["Turbo"].map, colormaps::maps["Turbo"].entryCount);

    sourcemenu::init();
    sinkmenu::init();
    bandplanmenu::init();
    displaymenu::init();
    vfo_color_menu::init();
    module_manager_menu::init();

    // TODO for 0.2.5
    // Fix gain not updated on startup, soapysdr

    // Update UI settings
    LoadingScreen::show("Loading configuration");
    core::configManager.acquire();

    double frequency = core::configManager.conf["frequency"];

    showMenu = core::configManager.conf["showMenu"];
    startedWithMenuClosed = !showMenu;

    gui::freqSelect.setFrequency(frequency);
    gui::freqSelect.frequencyChanged = false;
    sigpath::sourceManager.tune(frequency);
    gui::waterfall.setCenterFrequency(frequency);
    gui::waterfall.vfoFreqChanged = false;
    gui::waterfall.centerFreqMoved = false;
    gui::waterfall.selectFirstVFO();

    menuWidth = core::configManager.conf["menuWidth"];
    newWidth = menuWidth;

    fftHeight = core::configManager.conf["fftHeight"];
    gui::waterfall.setFFTHeight(fftHeight);

    tuningMode = core::configManager.conf["centerTuning"] ? tuner::TUNER_MODE_CENTER : tuner::TUNER_MODE_NORMAL;
    gui::waterfall.VFOMoveSingleClick = (tuningMode == tuner::TUNER_MODE_CENTER);

    core::configManager.release();

    // Correct the offset of all VFOs so that they fit on the screen
    float finalBwHalf = gui::waterfall.getBandwidth() / 2.0;
    for (auto& [_name, _vfo] : gui::waterfall.vfos) {
        if (_vfo->lowerOffset < -finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, (_vfo->bandwidth / 2) - finalBwHalf);
            continue;
        }
        if (_vfo->upperOffset > finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, finalBwHalf - (_vfo->bandwidth / 2));
            continue;
        }
    }

    autostart = core::args["autostart"].b();
    initComplete = true;

    core::moduleManager.doPostInitAll();
}

void MainWindow::shutdown() {
    onPlayStateChange.unbindAll();

    module_manager_menu::shutdown();
    vfo_color_menu::shutdown();
    displaymenu::shutdown();
    bandplanmenu::shutdown();
    sinkmenu::shutdown();
    sourcemenu::shutdown();

    sigpath::iqFrontEnd.stop();
    sigpath::iqFrontEnd.shutdown();

    fftwf_destroy_plan(fftwPlan);
    fftwPlan = NULL;
    fftwf_free(fft_out);
    fft_out = NULL;
    fftwf_free(fft_in);
    fft_in = NULL;

    gui::waterfall.shutdown();
}

float* MainWindow::acquireFFTBuffer(void* ctx) {
    return gui::waterfall.getFFTBuffer();
}

void MainWindow::releaseFFTBuffer(void* ctx) {
    gui::waterfall.pushFFT();
}

void MainWindow::vfoAddedHandler(VFOManager::VFO* vfo, void* ctx) {
    MainWindow* _this = (MainWindow*)ctx;
    std::string name = vfo->getName();
    core::configManager.acquire();
    if (!core::configManager.conf["vfoOffsets"].contains(name)) {
        core::configManager.release();
        return;
    }
    double offset = core::configManager.conf["vfoOffsets"][name];
    core::configManager.release();

    double viewBW = gui::waterfall.getViewBandwidth();
    double viewOffset = gui::waterfall.getViewOffset();

    double viewLower = viewOffset - (viewBW / 2.0);
    double viewUpper = viewOffset + (viewBW / 2.0);

    double newOffset = std::clamp<double>(offset, viewLower, viewUpper);

    sigpath::vfoManager.setCenterOffset(name, _this->initComplete ? newOffset : offset);
}

void MainWindow::draw() {
    const ImGuiViewport* const vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGui::Begin("Main", NULL, WINDOW_FLAGS);
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImGui::WaterfallVFO* vfo = NULL;
    if (!gui::waterfall.selectedVFO.empty()) {
        vfo = gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }

    // Handle VFO movement
    if (vfo != NULL) {
        if (vfo->centerOffsetChanged) {
            if (tuningMode == tuner::TUNER_MODE_CENTER) {
                tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::waterfall.getCenterFrequency() + vfo->generalOffset);
            }
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + vfo->generalOffset);
            gui::freqSelect.frequencyChanged = false;
            core::configManager.acquire();
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            core::configManager.release(true);
        }
    }

    sigpath::vfoManager.updateFromWaterfall(&gui::waterfall);

    // Handle selection of another VFO
    if (gui::waterfall.selectedVFOChanged) {
        gui::freqSelect.setFrequency((vfo != NULL) ? (vfo->generalOffset + gui::waterfall.getCenterFrequency()) : gui::waterfall.getCenterFrequency());
        gui::waterfall.selectedVFOChanged = false;
        gui::freqSelect.frequencyChanged = false;
    }

    // Handle change in selected frequency
    if (gui::freqSelect.frequencyChanged) {
        gui::freqSelect.frequencyChanged = false;
        tuner::tune(tuningMode, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
        if (vfo != NULL) {
            vfo->centerOffsetChanged = false;
            vfo->lowerOffsetChanged = false;
            vfo->upperOffsetChanged = false;
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        if (vfo != NULL) {
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
        }
        core::configManager.release(true);
    }

    // Handle dragging the frequency scale
    if (gui::waterfall.centerFreqMoved) {
        gui::waterfall.centerFreqMoved = false;
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        if (vfo != NULL) {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + vfo->generalOffset);
        }
        else {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency());
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        core::configManager.release(true);
    }

    int _fftHeight = gui::waterfall.getFFTHeight();
    if (fftHeight != _fftHeight) {
        fftHeight = _fftHeight;
        core::configManager.acquire();
        core::configManager.conf["fftHeight"] = fftHeight;
        core::configManager.release(true);
    }

    processMouseInputs = canProcessMouseInputs();
    processKeyboardInputs = canProcessKeyboardInputs();
    gui::menu.canDragMenuItems = processMouseInputs && processKeyboardInputs;

    // To Bar
    // ImGui::BeginChild("TopBarChild", ImVec2(0, 49.0f * style::uiScale), false, ImGuiWindowFlags_HorizontalScrollbar);
    const float btnBorderSz = 2.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(btnBorderSz, btnBorderSz));
    ImVec2 btnSize(30 * style::uiScale, 30 * style::uiScale);

    if (ImGui::ImageButton("sdrpp_menu_btn", icons::MENU, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), textCol) || (processKeyboardInputs && ImGui::IsKeyPressed(ImGuiKey_Menu, false))) {
        showMenu = !showMenu;
        core::configManager.acquire();
        core::configManager.conf["showMenu"] = showMenu;
        core::configManager.release(true);
    }

    ImGui::SameLine();

    bool tmpPlaySate = playing;
    if (playButtonLocked && !tmpPlaySate) { style::beginDisabled(); }
    if (playing) {
        if (ImGui::ImageButton("sdrpp_stop_btn", icons::STOP, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), textCol) || (processKeyboardInputs && ImGui::IsKeyPressed(ImGuiKey_End, false))) {
            setPlayState(false);
        }
    }
    else { // TODO: Might need to check if there even is a device
        if (ImGui::ImageButton("sdrpp_play_btn", icons::PLAY, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), textCol) || (processKeyboardInputs && ImGui::IsKeyPressed(ImGuiKey_End, false))) {
            setPlayState(true);
        }
    }

    if (playButtonLocked && !tmpPlaySate) { style::endDisabled(); }
    ImGui::PopStyleVar();

    // Handle auto-start
    if (autostart) {
        autostart = false;
        setPlayState(true);
    }

    ImGui::SameLine();
    float origY = ImGui::GetCursorPosY();

    sigpath::sinkManager.showVolumeSlider(gui::waterfall.selectedVFO, "##_sdrpp_main_volume_", 248 * style::uiScale, btnSize.y, btnBorderSz, true);

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY + btnBorderSz);
    gui::freqSelect.draw();

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(btnBorderSz, btnBorderSz));

    if (tuningMode == tuner::TUNER_MODE_CENTER) {
        if (ImGui::ImageButton("sdrpp_ena_st_btn", icons::CENTER_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_NORMAL;
            gui::waterfall.VFOMoveSingleClick = false;
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = false;
            core::configManager.release(true);
        }
    }
    else { // TODO: Might need to check if there even is a device
        if (ImGui::ImageButton("sdrpp_dis_st_btn", icons::NORMAL_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_CENTER;
            gui::waterfall.VFOMoveSingleClick = true;
            tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = true;
            core::configManager.release(true);
        }
    }

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    if (ImGui::ImageButton("sdrpp_about_btn", icons::ABOUT, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), textCol)) {
        openCredits = true;
    }

    ImGui::PopStyleVar();
    ImGui::SameLine();

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetStyle().ItemSpacing.x * style::uiScale));
    ImGui::SetCursorPosY(origY + btnBorderSz);

    float snrWidth = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("0.").x;
    ImGui::SetNextItemWidth(snrWidth);

    ImGui::SNRMeter((vfo != NULL) ? gui::waterfall.selectedVFOSNR : 0);

    // ImGui::EndChild();

    if (credits::isOpen && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        credits::isOpen = false;
    }

    // Reset input locks
    if (credits::isOpen) {
        processMouseInputs = false;
        processKeyboardInputs = false;
    }

    // Process menu keybinds
    if (processKeyboardInputs) {
        displaymenu::checkKeybinds();
    }

    ImVec2 winSize = ImGui::GetContentRegionMax();

    // Left Column
    if (showMenu) {
        ImGui::Columns(2, "WindowColumns", false);
        ImGui::SetColumnWidth(0, menuWidth);

        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.5f * style::uiScale);
        ImGui::BeginChild("Left Column", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        if (gui::menu.draw(firstMenuRender)) {
            core::configManager.acquire();
            json arr = json::array();
            for (int i = 0; i < gui::menu.order.size(); i++) {
                arr[i]["name"] = gui::menu.order[i].name;
                arr[i]["open"] = gui::menu.order[i].open;
            }
            core::configManager.conf["menuElements"] = arr;

            // Update enabled and disabled modules
            for (auto [_name, inst] : core::moduleManager.instances) {
                if (!core::configManager.conf["moduleInstances"].contains(_name)) { continue; }
                core::configManager.conf["moduleInstances"][_name]["enabled"] = inst.instance->isEnabled();
            }

            core::configManager.release(true);
        }
        if (startedWithMenuClosed) {
            startedWithMenuClosed = false;
        }
        else {
            firstMenuRender = false;
        }

        if (ImGui::CollapsingHeader("Debug")) {
            ImGui::Text("Frame time: %.3f ms", ImGui::GetIO().DeltaTime * 1000.0f);
            ImGui::Text("Frame rate: %.1f FPS", ImGui::GetIO().Framerate);
            ImGui::Text("Center frequency: %.0f Hz", gui::waterfall.getCenterFrequency());
            ImGui::Text("Source name: %s", sourceName.c_str());
            ImGui::Checkbox("Show ImGui Demo window", &showImGuiDemo);
            ImGui::Checkbox("Show ImPlot Demo window", &showImPlotDemo);

            // ImGui::Checkbox("Bypass buffering", &sigpath::iqFrontEnd.inputBuffer.bypass);
            // ImGui::Text("Buffering: %d", (sigpath::iqFrontEnd.inputBuffer.writeCur - sigpath::iqFrontEnd.inputBuffer.readCur + 32) % 32);

            ImGui::Checkbox("Use VSync", &backend::vsyncEnabled);
            ImGui::Checkbox("Waterfall single click", &gui::waterfall.VFOMoveSingleClick);

            if (ImGui::Button("Draw main menu first")) {
                gui::menu.order[0].open = true;
                firstMenuRender = true;
            }

            ImGui::Spacing();
        }

        ImGui::EndChild();

        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();

        const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(gui::themeManager.getCoreColor(ThemeManager::CoreCol_MainBorderColor));
        ImGui::GetWindowDrawList()->AddRect(rectMin, rectMax, borderColor, 1.0f * style::uiScale, 0, style::uiScale);
    }
    else {
        // When hiding the menu bar, just 1 column (waterfall/fft display) at that point
        ImGui::Columns(1, "WindowColumns", false);
    }

    // Note: need to divide item spacing on X by the number of columns in order
    // to have correct spacing on each side.
    const ImVec2& itemSpacing = ImGui::GetStyle().ItemSpacing;
    const float columnItemSpacingX = itemSpacing.x / 2;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(columnItemSpacingX, itemSpacing.y));

    // Right Column
    ImGui::NextColumn();

    // Handle menu resize
    if (processMouseInputs && showMenu) {
        const float curY = ImGui::GetCursorPosY();
        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        ImVec2 winPos = ImGui::GetWindowPos();
        winPos.x += columnItemSpacingX;
        winPos.y -= 1.0f; // Account for line cursor.
        const ImVec2 mousePos = ImGui::GetMousePos();
        const ImVec2 mouseLocal = mousePos - winPos;
        if (grabbingMenu) {
            newWidth = std::clamp<float>(mouseLocal.x, 250, winSize.x - 250);
            ImGui::GetForegroundDrawList()->AddLine(
                ImVec2(winPos.x + newWidth, winPos.y + curY),
                ImVec2(winPos.x + newWidth, winPos.y + winSize.y),
                ImGui::GetColorU32(ImGuiCol_SeparatorActive), style::uiScale);
        }
        if (mouseLocal.x >= newWidth - (2.0f * style::uiScale) &&
            mouseLocal.x <= newWidth + (2.0f * style::uiScale) &&
            mouseLocal.y > curY) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (click) {
                grabbingMenu = true;
            }
        }
        else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
        if (!down && grabbingMenu) {
            grabbingMenu = false;
            menuWidth = newWidth;
            core::configManager.acquire();
            core::configManager.conf["menuWidth"] = menuWidth;
            core::configManager.release(true);
        }
    }

    ImGui::PopStyleVar();
    ImGui::BeginChild("Waterfall");

    gui::waterfall.draw();

    ImGui::EndChild();

    // Handle arrow keys
    if (processKeyboardInputs && (vfo != NULL && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall))) {
        bool freqChanged = false;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !gui::freqSelect.digitHovered) {
            double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset - vfo->snapInterval;
            nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
            tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
            freqChanged = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !gui::freqSelect.digitHovered) {
            double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + vfo->snapInterval;
            nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
            tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
            freqChanged = true;
        }
        if (freqChanged) {
            core::configManager.acquire();
            core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
            if (vfo != NULL) {
                core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            }
            core::configManager.release(true);
        }
    }

    if (processMouseInputs) {
        // Handle scrollwheel
        int wheel = ImGui::GetIO().MouseWheel + ImGui::GetIO().MouseWheelH;
        if (wheel != 0 && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            double nfreq;
            if (vfo != NULL) {
                // Select factor depending on modifier keys
                double interval;
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                    interval = vfo->snapInterval * 10.0;
                }
                else if (ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                    interval = vfo->snapInterval * 0.1;
                }
                else {
                    interval = vfo->snapInterval;
                }

                nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + (interval * wheel);
                nfreq = roundl(nfreq / interval) * interval;
            }
            else {
                nfreq = gui::waterfall.getCenterFrequency() - (gui::waterfall.getViewBandwidth() * wheel / 20.0);
            }
            tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
            gui::freqSelect.setFrequency(nfreq);
            core::configManager.acquire();
            core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
            if (vfo != NULL) {
                core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            }
            core::configManager.release(true);
        }
    }

    ImGui::End();

    if (openCredits) {
        ImGui::OpenPopup("Credits");
        openCredits = false;
        credits::isOpen = true;
    }

    credits::show();

    if (showImGuiDemo) {
        ImGui::ShowDemoWindow(&showImGuiDemo);
    }
    if (showImPlotDemo) {
        ImPlot::ShowDemoWindow(&showImPlotDemo);
    }
}

bool MainWindow::canProcessMouseInputs() {
    return ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

bool MainWindow::canProcessKeyboardInputs() {
    return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
}

void MainWindow::muteInputThisFrame(bool mute) {
    processMouseInputs = !mute;
    processKeyboardInputs = !mute;
}

void MainWindow::setPlayState(bool _playing) {
    if (_playing == playing) { return; }
    if (_playing) {
        sigpath::iqFrontEnd.flushInputBuffer();
        sigpath::sourceManager.start();
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        playing = true;
        onPlayStateChange.emit(true);
    }
    else {
        playing = false;
        onPlayStateChange.emit(false);
        sigpath::sourceManager.stop();
        sigpath::iqFrontEnd.flushInputBuffer();
    }
}

bool MainWindow::sdrIsRunning() {
    return playing;
}

bool MainWindow::isPlaying() {
    return playing;
}

ImGui::WaterfallVFO* MainWindow::getSelectedVFO() {
    if (!gui::waterfall.selectedVFO.empty()) {
        return gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }

    return NULL;
}

void MainWindow::setFirstMenuRender() {
    firstMenuRender = true;
}