#pragma once
#include <imgui/imgui.h>
#include <fftw3.h>
#include <dsp/types.h>
#include <dsp/stream.h>
#include <signal_path/vfo_manager.h>
#include <string>
#include <utils/event.h>
#include <mutex>
#include <gui/tuner.h>

#define WINDOW_FLAGS ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground

class MainWindow {
public:
    void init();
    void draw();
    bool canProcessMouseInputs();
    bool canProcessKeyboardInputs();
    void muteInputThisFrame(bool mute);
    void setViewBandwidthSlider(float bandwidth);
    bool sdrIsRunning();
    void setFirstMenuRender();

    static float* acquireFFTBuffer(void* ctx);
    static void releaseFFTBuffer(void* ctx);

    // TODO: Replace with it's own class
    void setVFO(double freq);

    void setPlayState(bool _playing);
    bool isPlaying();

    bool processMouseInputs = false;
    bool processKeyboardInputs = false;
    bool playButtonLocked = false;

    Event<bool> onPlayStateChange;

private:
    static void vfoAddedHandler(VFOManager::VFO* vfo, void* ctx);

    // FFT Variables
    fftwf_complex *fft_in, *fft_out;
    fftwf_plan fftwPlan;
    std::mutex fft_mtx;

    // GUI Variables
    int fftSize = 8192 * 8;
    float fftMin = -70.0;
    float fftMax = 0.0;
    float bw = 8000000;

    std::string audioStreamName = "";
    std::string sourceName = "";

    int menuWidth = 300;
    int newWidth = 300;
    int fftHeight = 300;
    int tuningMode = tuner::TUNER_MODE_NORMAL;
    int selectedWindow = 0;

    bool playing = false;
    bool autostart = false;
    bool firstMenuRender = true;
    bool startedWithMenuClosed = false;
    bool grabbingMenu = false;
    bool openCredits = false;
    bool showMenu = true;
    bool showImGuiDemo = false;
    bool showImPlotDemo = false;
    bool initComplete = false;

    dsp::stream<dsp::complex_t> dummyStream;
    EventHandler<VFOManager::VFO*> vfoCreatedHandler;
};