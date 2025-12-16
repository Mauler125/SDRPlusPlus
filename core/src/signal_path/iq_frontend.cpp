#include "iq_frontend.h"
#include "../dsp/window/rectangular.h"
#include "../dsp/window/hann.h"
#include "../dsp/window/hamming.h"
#include "../dsp/window/blackman.h"
#include "../dsp/window/blackman_harris.h"
#include "../dsp/window/blackman_nuttall.h"
#include "../dsp/window/nuttall.h"
#include "../dsp/window/flat_top.h"
#include "../dsp/window/kaiser.h"
#include "../dsp/window/tukey.h"
#include "../dsp/window/gaussian.h"
#include "../dsp/window/bartlett.h"
#include "../dsp/window/bartlett_hann.h"
#include "../dsp/window/lanczos.h"
#include "../dsp/window/poisson.h"
#include "../dsp/window/half_sine.h"
#include <utils/flog.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <core.h>

IQFrontEnd::~IQFrontEnd() {
    if (!_init) { return; }
    stop();
    shutdown();
}

void IQFrontEnd::init(dsp::stream<dsp::complex_t>* in, double sampleRate, bool buffering, int decimRatio, bool dcBlocking, int fftSize, double fftRate, FFTWindow fftWindow, float* (*acquireFFTBuffer)(void* ctx), void (*releaseFFTBuffer)(void* ctx), void* fftCtx) {
    _sampleRate = sampleRate;
    _decimRatio = decimRatio;
    _fftSizeRequested = fftSize;
    _fftSize = fftSize;
    _fftRate = fftRate;
    _fftWindow = fftWindow;
    _acquireFFTBuffer = acquireFFTBuffer;
    _releaseFFTBuffer = releaseFFTBuffer;
    _fftCtx = fftCtx;

    effectiveSr = _sampleRate / _decimRatio;

    inBuf.init(in);
    inBuf.setBypass(!buffering);

    decim.init(NULL, _decimRatio);
    dcBlock.init(NULL, genDCBlockRate(effectiveSr));
    conjugate.init(NULL);

    preproc.init(inBuf.getOutStream());
    preproc.addBlock(&decim, _decimRatio > 1);
    preproc.addBlock(&dcBlock, dcBlocking);
    preproc.addBlock(&conjugate, false); // TODO: Replace by parameter

    split.init(preproc.out);

    // TODO: Do something to avoid basically repeating this code twice
    int skip;
    genReshapeParams(effectiveSr, _fftSize, _fftRate, skip, _nzFFTSize);
    reshape.init(&fftIn, fftSize, skip);
    fftSink.init(&reshape.out, handler, this);

    fftWindowBuf = dsp::buffer::alloc<float>(_nzFFTSize);
    generateFFTWindow();

    fftInBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    fftOutBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    fftwPlan = fftwf_plan_dft_1d(_fftSize, fftInBuf, fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);

    // Clear the rest of the FFT input buffer
    dsp::buffer::clear(fftInBuf, _fftSize - _nzFFTSize, _nzFFTSize);

    split.bindStream(&fftIn);

    _init = true;
}

void IQFrontEnd::shutdown() {
    _init = false;

    fftwf_destroy_plan(fftwPlan);
    fftwPlan = NULL;

    fftwf_free(fftOutBuf);
    fftOutBuf = NULL;

    fftwf_free(fftInBuf);
    fftInBuf = NULL;

    dsp::buffer::free(fftWindowBuf);
    fftWindowBuf = NULL;

    fftSink.shutdown();
    reshape.shutdown();
    split.shutdown();
    conjugate.shutdown();
    dcBlock.shutdown();
    decim.shutdown();
    inBuf.shutdown();

    vfoStreams.clear();
    vfos.clear();
}

void IQFrontEnd::setInput(dsp::stream<dsp::complex_t>* in) {
    inBuf.setInput(in);
}

void IQFrontEnd::setSampleRate(double sampleRate) {
    // Temp stop the necessary blocks
    dcBlock.tempStop();
    for (auto& [name, vfo] : vfos) {
        vfo->tempStop();
    }

    // Update the samplerate
    _sampleRate = sampleRate;
    effectiveSr = _sampleRate / _decimRatio;
    dcBlock.setRate(genDCBlockRate(effectiveSr));
    for (auto& [name, vfo] : vfos) {
        vfo->setInSamplerate(effectiveSr);
    }

    // Reconfigure the FFT
    updateFFTPath();

    // Restart blocks
    dcBlock.tempStart();
    for (auto& [name, vfo] : vfos) {
        vfo->tempStart();
    }
}

void IQFrontEnd::setBuffering(bool enabled) {
    inBuf.setBypass(!enabled);
}

void IQFrontEnd::setDecimation(int ratio) {
    // Temp stop the decimator
    decim.tempStop();

    // Update the decimation ratio
    _decimRatio = ratio;
    if (_decimRatio > 1) { decim.setRatio(_decimRatio); }
    setSampleRate(_sampleRate);

    // Restart the decimator if it was running
    decim.tempStart();

    // Enable or disable in the chain
    preproc.setBlockEnabled(&decim, _decimRatio > 1, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });

    // Update the DSP sample rate (TODO: Find a way to get rid of this)
    core::setInputSampleRate(_sampleRate);
}

void IQFrontEnd::setDCBlocking(bool enabled) {
    preproc.setBlockEnabled(&dcBlock, enabled, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });
}

void IQFrontEnd::setInvertIQ(bool enabled) {
    preproc.setBlockEnabled(&conjugate, enabled, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });
}

void IQFrontEnd::bindIQStream(dsp::stream<dsp::complex_t>* stream) {
    split.bindStream(stream);
}

void IQFrontEnd::unbindIQStream(dsp::stream<dsp::complex_t>* stream) {
    split.unbindStream(stream);
}

dsp::channel::RxVFO* IQFrontEnd::addVFO(const std::string& name, double sampleRate, double bandwidth, double offset) {
    // Make sure no other VFO with that name already exists
    if (vfos.find(name) != vfos.end()) {
        flog::error("[IQFrontEnd] Tried to add VFO with existing name.");
        return NULL;
    }

    // Create VFO and its input stream
    dsp::stream<dsp::complex_t>* vfoIn = new dsp::stream<dsp::complex_t>;
    dsp::channel::RxVFO* vfo = new dsp::channel::RxVFO(vfoIn, effectiveSr, sampleRate, bandwidth, offset);

    // Register them
    vfoStreams[name] = vfoIn;
    vfos[name] = vfo;
    bindIQStream(vfoIn);

    // Start VFO
    vfo->start();

    return vfo;
}

void IQFrontEnd::removeVFO(const std::string& name) {
    // Make sure that a VFO with that name exists
    if (vfos.find(name) == vfos.end()) {
        flog::error("[IQFrontEnd] Tried to remove a VFO that doesn't exist.");
        return;
    }

    // Remove the VFO and stream from registry
    dsp::stream<dsp::complex_t>* vfoIn = vfoStreams[name];
    dsp::channel::RxVFO* vfo = vfos[name];

    // Stop the VFO
    vfo->stop();

    unbindIQStream(vfoIn);
    vfoStreams.erase(name);
    vfos.erase(name);

    // Delete the VFO and its input stream
    delete vfo;
    delete vfoIn;
}

typedef double (*WindowFunc)(const double n, const double N, const void* const parms);

static inline WindowFunc getWindowFunction(const IQFrontEnd::FFTWindow type) {
    switch (type) {
    case IQFrontEnd::FFTWindow::RECTANGULAR:
        return dsp::window::rectangular;
    case IQFrontEnd::FFTWindow::HANN:
        return dsp::window::hann;
    case IQFrontEnd::FFTWindow::HAMMING:
        return dsp::window::hamming;
    case IQFrontEnd::FFTWindow::BLACKMAN:
        return dsp::window::blackman;
    case IQFrontEnd::FFTWindow::BLACKMAN_HARRIS:
        return dsp::window::blackmanHarris;
    case IQFrontEnd::FFTWindow::BLACKMAN_NUTTALL:
        return dsp::window::blackmanNuttall;
    case IQFrontEnd::FFTWindow::NUTTALL:
        return dsp::window::nuttall;
    case IQFrontEnd::FFTWindow::FLAT_TOP:
        return dsp::window::flatTop;
    case IQFrontEnd::FFTWindow::KAISER:
        return dsp::window::kaiser;
    case IQFrontEnd::FFTWindow::TUKEY:
        return dsp::window::tukey;
    case IQFrontEnd::FFTWindow::GAUSSIAN:
        return dsp::window::gaussian;
    case IQFrontEnd::FFTWindow::BARTLETT:
        return dsp::window::bartlett;
    case IQFrontEnd::FFTWindow::BARTLETT_HANN:
        return dsp::window::bartlettHann;
    case IQFrontEnd::FFTWindow::LANCZOS:
        return dsp::window::lanczos;
    case IQFrontEnd::FFTWindow::POISSON:
        return dsp::window::poisson;
    case IQFrontEnd::FFTWindow::HALF_SINE:
        return dsp::window::halfSine;
    default:
        assert(0);
        return dsp::window::rectangular;
    }
}

void* IQFrontEnd::getWindowParams(const FFTWindow type) {
    switch (type) {
    case FFTWindow::HANN:
        return &_hannParams;
    case FFTWindow::KAISER:
        return &_kaiserParams;
    case FFTWindow::TUKEY:
        return &_tukeyParams;
    case FFTWindow::GAUSSIAN:
        return &_gaussianParams;
    case FFTWindow::POISSON:
        return &_poissonParams;
    default:
        return NULL;
    }
}

static inline float fftShiftSign(const int idx) {
    return (idx % 2) ? -1.0f : 1.0f;
}

void IQFrontEnd::generateFFTWindow() {
    WindowFunc window = getWindowFunction(_fftWindow);
    void* const params = getWindowParams(_fftWindow);

    for (int i = 0; i < _nzFFTSize; i++) {
        fftWindowBuf[i] = window(i, _nzFFTSize, params) * fftShiftSign(i);
    }
}

static inline bool ImGui_SliderDouble(const char* label, double* v, double v_min, double v_max, const char* format, ImGuiSliderFlags flags) {
    return ImGui::SliderScalar(label, ImGuiDataType_Double, v, &v_min, &v_max, format, flags);
}

void IQFrontEnd::renderFFTWindowMenu() {
    bool parmChanged = false;

    switch (_fftWindow) {
    case FFTWindow::HANN:
        ImGui::LeftLabel("Hann Alpha");
        ImGui::FillWidth();
        parmChanged |= ImGui_SliderDouble("##iqfrontend_hann_alpha", &_hannParams.alpha, 0.0, 1.0, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        break;
    case FFTWindow::KAISER:
        ImGui::LeftLabel("Kaiser Beta");
        ImGui::FillWidth();
        parmChanged |= ImGui_SliderDouble("##iqfrontend_kaiser_beta", &_kaiserParams.beta, 0.0, 60.0, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        break;
    case FFTWindow::TUKEY:
        ImGui::LeftLabel("Tukey Alpha");
        ImGui::FillWidth();
        parmChanged |= ImGui_SliderDouble("##iqfrontend_tukey_alpha", &_tukeyParams.alpha, 0.0, 1.0, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        break;
    case FFTWindow::GAUSSIAN:
        ImGui::LeftLabel("Gaussian Sigma");
        ImGui::FillWidth();
        parmChanged |= ImGui_SliderDouble("##iqfrontend_gaussian_sigma", &_gaussianParams.sigma, 0.1, 0.5, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        break;
    case FFTWindow::POISSON:
        ImGui::LeftLabel("Poisson Alpha");
        ImGui::FillWidth();
        parmChanged |= ImGui_SliderDouble("##iqfrontend_poisson_alpha", &_poissonParams.alpha, 0.0, 5.0, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        break;
    }

    if (parmChanged) {
        updateFFTPath();
    }
}

void IQFrontEnd::setFFTSize(int size) {
    _fftSizeRequested = size;
    updateFFTPath(true);
}

void IQFrontEnd::setFFTRate(double rate) {
    _fftRate = rate;
    updateFFTPath();
}

void IQFrontEnd::setFFTWindow(FFTWindow fftWindow) {
    _fftWindow = fftWindow;
    updateFFTPath();
}

void IQFrontEnd::flushInputBuffer() {
    inBuf.flush();
}

void IQFrontEnd::start() {
    // Start input buffer
    inBuf.start();

    // Start pre-proc chain (automatically start all bound blocks)
    preproc.start();

    // Start IQ splitter
    split.start();

    // Start all VFOs
    for (auto& [name, vfo] : vfos) {
        vfo->start();
    }

    // Start FFT chain
    reshape.start();
    fftSink.start();
}

void IQFrontEnd::stop() {
    // Stop FFT chain
    fftSink.stop();
    reshape.stop();

    // Stop all VFOs
    for (auto& [name, vfo] : vfos) {
        vfo->stop();
    }

    // Stop IQ splitter
    split.stop();

    // Stop pre-proc chain (automatically start all bound blocks)
    preproc.stop();

    // Stop input buffer
    inBuf.stop();
}

double IQFrontEnd::getEffectiveSamplerate() {
    return effectiveSr;
}

void IQFrontEnd::handler(dsp::complex_t* data, int count, void* ctx) {
    IQFrontEnd* _this = (IQFrontEnd*)ctx;

    // Apply window
    volk_32fc_32f_multiply_32fc((lv_32fc_t*)_this->fftInBuf, (lv_32fc_t*)data, _this->fftWindowBuf, _this->_nzFFTSize);

    // Execute FFT
    fftwf_execute(_this->fftwPlan);

    // Aquire buffer
    float* fftBuf = _this->_acquireFFTBuffer(_this->_fftCtx);

    // Convert the complex output of the FFT to dB amplitude
    if (fftBuf) {
        volk_32fc_s32f_power_spectrum_32f(fftBuf, (lv_32fc_t*)_this->fftOutBuf, _this->_fftSize, _this->_fftSize);
    }

    // Release buffer
    _this->_releaseFFTBuffer(_this->_fftCtx);
}

void IQFrontEnd::updateFFTPath(bool updateWaterfall) {
    // Temp stop branch
    reshape.tempStop();
    fftSink.tempStop();

    // This must be done here because it is still possible for handler() to be
    // called during thread syncing, in which we still need to retain the prev
    // buffer size as we haven't reallocated buffers yet.
    _fftSize = _fftSizeRequested;

    // Update reshaper settings
    int skip;
    genReshapeParams(effectiveSr, _fftSize, _fftRate, skip, _nzFFTSize);
    reshape.setKeep(_nzFFTSize);
    reshape.setSkip(skip);

    // Update window
    dsp::buffer::free(fftWindowBuf);
    fftWindowBuf = dsp::buffer::alloc<float>(_nzFFTSize);
    generateFFTWindow();

    // Update FFT plan
    fftwf_free(fftInBuf);
    fftInBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    fftwf_free(fftOutBuf);
    fftOutBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    fftwf_destroy_plan(fftwPlan);
    fftwPlan = fftwf_plan_dft_1d(_fftSize, fftInBuf, fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);

    // Clear the rest of the FFT input buffer
    dsp::buffer::clear(fftInBuf, _fftSize - _nzFFTSize, _nzFFTSize);

    // Update waterfall (TODO: This is annoying, it makes this module non testable and will constantly clear the waterfall for any reason)
    if (updateWaterfall) { gui::waterfall.setRawFFTSize(_fftSize); }

    // Restart branch
    reshape.tempStart();
    fftSink.tempStart();
}