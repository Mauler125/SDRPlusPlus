#include <gui/widgets/waterfall.h>
#include <gui/widgets/crosshair.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imutils.h>
#include <algorithm>
#include <volk/volk.h>
#include <utils/flog.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/tools.h>
#include <../backends/gfx.h>

static const float s_defaultColorMap[][3] = {
    { 0x00, 0x00, 0x20 },
    { 0x00, 0x00, 0x30 },
    { 0x00, 0x00, 0x50 },
    { 0x00, 0x00, 0x91 },
    { 0x1E, 0x90, 0xFF },
    { 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0x00 },
    { 0xFE, 0x6D, 0x16 },
    { 0xFF, 0x00, 0x00 },
    { 0xC6, 0x00, 0x00 },
    { 0x9F, 0x00, 0x00 },
    { 0x75, 0x00, 0x00 },
    { 0x4A, 0x00, 0x00 }
};

// TODO: Fix this hacky BS

static const double s_frequencyRanges[] = {
    1.0, 2.0, 2.5, 5.0,
    10.0, 20.0, 25.0, 50.0,
    100.0, 200.0, 250.0, 500.0,
    1000.0, 2000.0, 2500.0, 5000.0,
    10000.0, 20000.0, 25000.0, 50000.0,
    100000.0, 200000.0, 250000.0, 500000.0,
    1000000.0, 2000000.0, 2500000.0, 5000000.0,
    10000000.0, 20000000.0, 25000000.0, 50000000.0
};

static inline double findBestFrequencyRange(double bandwidth, int maxSteps) {
    for (int i = 0; i < std::size(s_frequencyRanges); i++) {
        if (bandwidth / s_frequencyRanges[i] < (double)maxSteps) {
            return s_frequencyRanges[i];
        }
    }

    return s_frequencyRanges[std::size(s_frequencyRanges) - 1];
}

static const double s_timeRanges[] = {
    1.0, 2.0, 3.0, 5.0, 8.0, 10.0, 12.0, 15.0,
    20.0, 25.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0,
    150.0, 200.0, 250.0, 300.0, 400.0, 500.0, 750.0, 1000.0,
    2000.0, 5000.0, 10000.0, 20000.0, 30000.0, 60000.0, 120000.0, 300000.0
};

static inline double findBestTimeRange(double timeSpan, int maxSteps) {
    for (int i = 0; i < std::size(s_timeRanges); i++) {
        if (timeSpan / s_timeRanges[i] < (double)maxSteps) {
            return s_timeRanges[i];
        }
    }
    return s_timeRanges[std::size(s_timeRanges) - 1];
}

static inline int printScaledFrequency(double freq, char* buf, const int bufLen) {
    double freqAbs = fabs(freq);
    int ret = 0;
    if (freqAbs < 1000) {
        ret = snprintf(buf, bufLen, "%.6gHz", freq);
    }
    else if (freqAbs < 1000000) {
        ret = snprintf(buf, bufLen, "%.6lgKHz", freq / 1000.0);
    }
    else if (freqAbs < 1000000000) {
        ret = snprintf(buf, bufLen, "%.6lgMHz", freq / 1000000.0);
    }
    else if (freqAbs < 1000000000000) {
        ret = snprintf(buf, bufLen, "%.6lgGHz", freq / 1000000000.0);
    }

    return std::clamp(ret, 0, bufLen);
}

static inline int printScaledTime(double ms, char* buf, const int bufLen) {
    const double seconds = ms / 1000.0;
    int ret = 0;
    if (seconds >= 3600.0) {
        const double hours = seconds / 3600.0;
        ret = snprintf(buf, bufLen, "%.2fhrs", hours);
    }
    else if (seconds >= 60.0) {
        const double minutes = seconds / 60.0;
        ret = snprintf(buf, bufLen, "%.2fmin", minutes);
    }
    else {
        ret = snprintf(buf, bufLen, "%.2fsec", seconds);
    }

    return std::clamp(ret, 0, bufLen);
}

static inline void doZoom(int offset, int width, int inSize, int outSize, float* in, float* out) {
    // NOTE: REMOVE THAT SHIT, IT'S JUST A HACKY FIX
    if (offset < 0) {
        offset = 0;
    }
    if (width > 524288) {
        width = 524288;
    }

    float* bufEnd = in + inSize;
    double factor = (double)width / (double)outSize; // The output "FFT" is `factor` times smaller than the input.
    double id = offset;

    for (int i = 0; i < outSize; i++) {
        // For each pixel on the output, "window" the source FFT datapoints (starting from `&data[(int) id]`
        // and ending at `searchEnd = &data[(int) (id + factor)]`). Then find the highest peak in the range.
        // The fractional part is discarded in the cast, so with zoomed-in view (`factor` < 1), pixels are "stretched".
        // So with `factor` == 0.5, one pixel is `data[(int) 69]`, and the very next one is `data[(int) 69.5]`.
        float* cursor = in + (int)id;
        float* searchEnd = cursor + (int)factor;
        if (searchEnd > bufEnd) { // This compiles into `cmp` and `cmovbe`, non-branching instructions.
            searchEnd = bufEnd;
        }

        float maxVal = *cursor;
        while (cursor != searchEnd) {
            if (*cursor > maxVal) { maxVal = *cursor; }
            cursor++;
        }

        out[i] = maxVal;
        id += factor;
    }
}

namespace ImGui {
    WaterFall::WaterFall() {
        fftMin = -70.0;
        fftMax = 0.0;
        waterfallMin = -70.0;
        waterfallMax = 0.0;
        FFTAreaHeight = 300;
        newFFTAreaHeight = FFTAreaHeight;
        fftHeight = FFTAreaHeight - 50;
        dataWidth = 600;
        lastWidgetPos.x = 0;
        lastWidgetPos.y = 0;
        lastWidgetSize.x = 0;
        lastWidgetSize.y = 0;
        latestFFT = new float[dataWidth];
        latestFFTHold = new float[dataWidth];
        tempZoomFFT = new float[dataWidth];
        inputBuffer = new float[1];

        viewBandwidth = 1.0;
        wholeBandwidth = 1.0;

        lastScaleUpdateTime = std::chrono::steady_clock::now() - std::chrono::hours(1);
        updatePallette(&s_defaultColorMap[0][0], std::size(s_defaultColorMap));
    }

    WaterFall::~WaterFall() {
        if (latestFFT) {
            delete[] latestFFT;
        }
        if (latestFFTHold) {
            delete[] latestFFTHold;
        }
        if (tempZoomFFT) {
            delete[] tempZoomFFT;
        }
        if (inputBuffer) {
            delete[] inputBuffer;
        }
        if (rawFFTs) {
            free(rawFFTs);
        }
        shutdown();
    }

    bool WaterFall::init(const std::string& resDir) {
        if (!std::filesystem::is_directory(resDir)) {
            flog::error("Invalid resource directory: {0}", resDir);
            return false;
        }

        GLuint vertex = 0;
        if (!Gfx_LoadAndCompileShader(GL_VERTEX_SHADER, (resDir + "/shaders/waterfall_screen_quad_vs.glsl"), vertex)) {
            return false;
        }

        GLuint fragment = 0;
        if (!Gfx_LoadAndCompileShader(GL_FRAGMENT_SHADER, (resDir + "/shaders/waterfall_ring_buffer_ps.glsl"), fragment)) {
            glDeleteShader(vertex);
            return false;
        }

        bool linkSuccess = Gfx_CreateAndLinkProgram(vertex, fragment, wfShaderProgram);

        glDeleteShader(vertex);
        glDeleteShader(fragment);

        if (!linkSuccess) {
            return false;
        }

        Gfx_CreateScreenQuad(wfVao, wfVbo);

        Gfx_CreateTexture2D(wfPaletteTexture, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
        Gfx_CreateTexture2D(wfFboTexture, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);

        // Scroll tex
        Gfx_CreateTexture2D(wfRawDataTexture, GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_REPEAT);
        Gfx_CreateFramebuffer(wfFbo);

        return true;
    }

    void WaterFall::shutdown() {
        if (wfShaderProgram) {
            glDeleteProgram(wfShaderProgram);
            wfShaderProgram = 0;
        }
        if (wfVao) {
            glDeleteVertexArrays(1, &wfVao);
            wfVao = 0;
        }
        if (wfVbo) {
            glDeleteBuffers(1, &wfVbo);
            wfVbo = 0;
        }
        if (wfFbo) {
            glDeleteFramebuffers(1, &wfFbo);
            wfFbo = 0;
        }
        if (wfFboTexture) {
            glDeleteTextures(1, &wfFboTexture);
            wfFboTexture = 0;
        }
        if (wfRawDataTexture) {
            glDeleteTextures(1, &wfRawDataTexture);
            wfRawDataTexture = 0;
        }
        if (wfPaletteTexture) {
            glDeleteTextures(1, &wfPaletteTexture);
            wfPaletteTexture = 0;
        }
    }

    static inline void emitShadowPrim(
        float x1,
        float y1,
        float y2,
        ImU32 col,
        const ImVec2& uv,
        ImDrawVert*& vtx,
        ImDrawIdx*& idx,
        ImDrawIdx& vtx_idx) {
        const float x2 = x1 + 1.0f;

        vtx[0] = { { x1, y1 }, uv, col };
        vtx[1] = { { x2, y1 }, uv, col };
        vtx[2] = { { x2, y2 }, uv, col };
        vtx[3] = { { x1, y2 }, uv, col };
        vtx += 4;

        idx[0] = vtx_idx + 0;
        idx[1] = vtx_idx + 1;
        idx[2] = vtx_idx + 2;
        idx[3] = vtx_idx + 0;
        idx[4] = vtx_idx + 2;
        idx[5] = vtx_idx + 3;
        idx += 6;

        vtx_idx += 4;
    }

    // Taken from imgui_draw.cpp
    #define IM_NORMALIZE2F_OVER_ZERO(VX, VY) \
    {                                    \
        float d2 = VX * VX + VY * VY;    \
        if (d2 > 0.0f) {                 \
            float inv_len = ImRsqrt(d2); \
            VX *= inv_len;               \
            VY *= inv_len;               \
        }                                \
    }                                    \
    (void)0

    static inline void emitAALineSegment(
        ImDrawList* draw,
        const ImVec2& p1,
        const ImVec2& p2,
        ImU32 col,
        float thickness,
        ImDrawVert*& vtx,
        ImDrawIdx*& idx,
        ImDrawIdx& vtx_idx) {
        const float AA_SIZE = draw->_FringeScale;
        const ImVec2 uv = draw->_Data->TexUvWhitePixel;
        const ImU32 col_trans = col & ~IM_COL32_A_MASK;

        float dx = p2.x - p1.x;
        float dy = p2.y - p1.y;
        IM_NORMALIZE2F_OVER_ZERO(dx, dy);

        float nx = dy * AA_SIZE;
        float ny = -dx * AA_SIZE;

        vtx[0] = { p1, uv, col };
        vtx[1] = { { p1.x + nx, p1.y + ny }, uv, col_trans };
        vtx[2] = { { p1.x - nx, p1.y - ny }, uv, col_trans };

        vtx[3] = { p2, uv, col };
        vtx[4] = { { p2.x + nx, p2.y + ny }, uv, col_trans };
        vtx[5] = { { p2.x - nx, p2.y - ny }, uv, col_trans };

        idx[0] = vtx_idx + 0;
        idx[1] = vtx_idx + 1;
        idx[2] = vtx_idx + 4;
        idx[3] = vtx_idx + 0;
        idx[4] = vtx_idx + 4;
        idx[5] = vtx_idx + 3;

        idx[6] = vtx_idx + 0;
        idx[7] = vtx_idx + 3;
        idx[8] = vtx_idx + 5;
        idx[9] = vtx_idx + 0;
        idx[10] = vtx_idx + 5;
        idx[11] = vtx_idx + 2;

        vtx += 6;
        idx += 12;
        vtx_idx += 6;
    }

    static inline void emitFFTLineSegment(
        ImDrawList* drawList,
        float x,
        float fftValue,
        float yScaleBase,
        float scaleFactor,
        float areaMinY,
        float areaMaxY,
        ImU32 col,
        ImVec2& prevPos,
        ImDrawVert*& vtx,
        ImDrawIdx*& idx,
        ImDrawIdx& vtx_idx) {
        const float y = std::clamp(yScaleBase - (fftValue * scaleFactor), areaMinY, areaMaxY);
        ImVec2 currPos(x, roundf(y));

        // From ImGui::AddLine()
        const ImVec2 p0 = prevPos + ImVec2(0.5f, 0.5f);
        const ImVec2 p1 = currPos + ImVec2(0.5f, 0.5f);

        emitAALineSegment(drawList, p0, p1, col, 1.0f, vtx, idx, vtx_idx);
        prevPos = currPos;
    }

    // IMPORTANT: line rendering here is done manually, because calling AddLine()
    // for each line was very slow. Most of the techniques are just taken from
    // imgui internals. This was tricky to get right because of anti-aliasing,
    // but this manual approach significantly boosted perf so if you want to add
    // stuff make sure you use (or adjust) the statics above to keep this fast.
    // Also, i tried doing manual unrolls here but it barely gave 2uS boost over
    // 500 frames on avg on the several cpu's i've tested. SIMD + unroll might be
    // worth exploring in the future to make it even faster.
    void WaterFall::renderSpectrumGeometry(const float scaleFactor) {
        if (!latestFFT || fftLines == 0) {
            return;
        }

        const float areaMinY = fftAreaMin.y + 1.0f;
        const float areaMaxY = fftAreaMax.y;
        const float areaMinX = fftAreaMin.x;
        const float yScaleBase = areaMaxY + (fftMin * scaleFactor);

        ImDrawList* drawList = window->DrawList;
        const int shadowCount = dataWidth - 1;

        if (shadowCount > 0) {
            drawList->PrimReserve(shadowCount * 6, shadowCount * 4);
            const ImU32 colShadow = ImGui::GetColorU32(ImGuiCol_PlotLines, 0.2f);

            ImDrawVert* vtx_ptr = drawList->_VtxWritePtr;
            ImDrawIdx* idx_ptr = drawList->_IdxWritePtr;
            ImDrawIdx vtx_idx = drawList->_VtxCurrentIdx;
            const ImVec2 uv = drawList->_Data->TexUvWhitePixel;

            drawList->_VtxWritePtr += shadowCount * 4;
            drawList->_IdxWritePtr += shadowCount * 6;
            drawList->_VtxCurrentIdx += shadowCount * 4;

            for (int i = 1; i <= shadowCount; i++) {
                const float y = std::clamp(yScaleBase - (latestFFT[i] * scaleFactor), areaMinY, areaMaxY);
                emitShadowPrim(areaMinX + (float)i, roundf(y), areaMaxY, colShadow, uv, vtx_ptr, idx_ptr, vtx_idx);
            }
        }

        const int segmentCount = dataWidth - 1;

        if (segmentCount > 0) {
            const bool showHold = fftHold && latestFFTHold;
            const int totalSegments = segmentCount + (showHold ? segmentCount : 0);

            drawList->PrimReserve(totalSegments * 12, totalSegments * 6);
            const ImU32 colTrace = ImGui::GetColorU32(ImGuiCol_PlotLines);

            ImDrawVert* vtx = drawList->_VtxWritePtr;
            ImDrawIdx* idx = drawList->_IdxWritePtr;
            ImDrawIdx vtx_idx = drawList->_VtxCurrentIdx;

            drawList->_VtxWritePtr += totalSegments * 6;
            drawList->_IdxWritePtr += totalSegments * 12;
            drawList->_VtxCurrentIdx += totalSegments * 6;

            const float prevY = std::clamp(yScaleBase - (latestFFT[0] * scaleFactor), areaMinY, areaMaxY);
            ImVec2 prevPos(areaMinX, roundf(prevY));

            for (int i = 1; i <= segmentCount; i++) {
                emitFFTLineSegment(drawList, areaMinX + (float)i, latestFFT[i], yScaleBase, scaleFactor, areaMinY, areaMaxY, colTrace, prevPos, vtx, idx, vtx_idx);
            }

            if (showHold) {
                const ImU32 colHold = ImGui::ColorConvertFloat4ToU32(gui::themeManager.getCoreColor(ThemeManager::CoreCol_FFTHoldColor));
                const float prevHoldY = std::clamp(yScaleBase - (latestFFTHold[0] * scaleFactor), areaMinY, areaMaxY);

                ImVec2 prevHoldPos(areaMinX, roundf(prevHoldY));

                for (int i = 1; i <= segmentCount; i++) {
                    emitFFTLineSegment(drawList, areaMinX + (float)i, latestFFTHold[i], yScaleBase, scaleFactor, areaMinY, areaMaxY, colHold, prevHoldPos, vtx, idx, vtx_idx);
                }
            }
        }
    }

    void WaterFall::drawFFT() {
        std::lock_guard<std::recursive_mutex> lck(latestFFTMtx);
        // Calculate scaling factor
        float startLine = floorf(fftMax / verticalFftRange) * verticalFftRange;
        float vertRange = fftMax - fftMin;
        float scaleFactor = fftHeight / vertRange;
        char buf[128];
        const int bufLen = (int)sizeof(buf);

        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        float textVOffset = 10.0f * style::uiScale;

        // Vertical scale
        float scaleTickOfsset = 7 * style::uiScale;
        for (float line = startLine; line > fftMin; line -= verticalFftRange) {
            float yPos = fftAreaMax.y - ((line - fftMin) * scaleFactor);
            window->DrawList->AddLine(ImVec2(fftAreaMin.x, roundf(yPos)),
                                      ImVec2(fftAreaMax.x, roundf(yPos)),
                                      IM_COL32(50, 50, 50, 255), style::uiScale);
            window->DrawList->AddLine(ImVec2(fftAreaMin.x, roundf(yPos)),
                                      ImVec2(fftAreaMin.x - scaleTickOfsset, roundf(yPos)),
                                      text, style::uiScale);
            const int textLen = std::clamp(sprintf(buf, "%ddBFS", (int)roundf(line)), 0, bufLen);
            ImVec2 txtSz = ImGui::CalcTextSize(buf);
            window->DrawList->AddText(ImVec2(fftAreaMin.x - txtSz.x - textVOffset, roundf(yPos - (txtSz.y / 2.0))), text, buf, &buf[textLen]);
        }

        // Horizontal scale
        double startFreq = ceilf(lowerFreq / range) * range;
        double horizScale = (double)dataWidth / viewBandwidth;
        for (double freq = startFreq; freq < upperFreq; freq += range) {
            double xPos = fftAreaMin.x + ((freq - lowerFreq) * horizScale);
            window->DrawList->AddLine(ImVec2(roundf(xPos), fftAreaMin.y + 1),
                                      ImVec2(roundf(xPos), fftAreaMax.y),
                                      IM_COL32(50, 50, 50, 255), style::uiScale);
            window->DrawList->AddLine(ImVec2(roundf(xPos), fftAreaMax.y),
                                      ImVec2(roundf(xPos), fftAreaMax.y + scaleTickOfsset),
                                      text, style::uiScale);
            const int textLen = printScaledFrequency(freq, buf, bufLen);
            ImVec2 txtSz = ImGui::CalcTextSize(buf, &buf[textLen]);
            window->DrawList->AddText(ImVec2(roundf(xPos - (txtSz.x / 2.0)), fftAreaMax.y + txtSz.y), text, buf, &buf[textLen]);
        }

        renderSpectrumGeometry(scaleFactor);

        FFTRedrawArgs args;
        args.min = fftAreaMin;
        args.max = fftAreaMax;
        args.lowFreq = lowerFreq;
        args.highFreq = upperFreq;
        args.freqToPixelRatio = horizScale;
        args.pixelToFreqRatio = viewBandwidth / (double)dataWidth;
        args.window = window;
        onFFTRedraw.emit(args);

        if (gui::mainWindow.processMouseInputs) {
            DrawCrosshairUnderCursor(ImRect(fftAreaMin, fftAreaMax), ImGui::ColorConvertFloat4ToU32(gui::themeManager.getCoreColor(ThemeManager::CoreCol_CrosshairColor)), 1.0f, crosshairFlags);
        }

        // X Axis
        window->DrawList->AddLine(ImVec2(fftAreaMin.x, fftAreaMax.y),
                                  ImVec2(fftAreaMax.x, fftAreaMax.y),
                                  text, style::uiScale);
        // Y Axis
        window->DrawList->AddLine(ImVec2(fftAreaMin.x, fftAreaMin.y),
                                  ImVec2(fftAreaMin.x, fftAreaMax.y - 1),
                                  text, style::uiScale);
    }

    void WaterFall::fixupTimestamps() {
        const bool isPlaying = gui::mainWindow.isPlaying();

        if (wasPlaying && !isPlaying) {
            pauseStartTime = std::chrono::steady_clock::now();
        }
        else if (!wasPlaying && isPlaying) {
            std::lock_guard<std::mutex> lck(timestampMtx);

            if (fftTimestamps.size() > 1) { // Need at least 2 points
                const auto resumeTime = std::chrono::steady_clock::now();
                const auto pauseDuration = resumeTime - pauseStartTime;

                for (auto& ts : fftTimestamps) {
                    // NOTE: if the system plays again, its possible for fft's
                    // to be added before we draw. These time stamps shouldn't
                    // be shifted back!
                    if (ts < pauseStartTime) {
                        ts += pauseDuration;
                    }
                }

                const auto newestTime = fftTimestamps.front();
                const auto oldestVisibleTime = fftTimestamps.back();
                const auto visibleDuration = newestTime - oldestVisibleTime;
                const float actualTotalTimeMs = std::chrono::duration_cast<std::chrono::microseconds>(visibleDuration).count() / 1000.0f;

                if (actualTotalTimeMs > 0) {
                    currentPixelsPerMs = (float)waterfallHeight / actualTotalTimeMs;
                }

                lastScaleUpdateTime = resumeTime;
            }
            else {
                fftTimestamps.clear();
            }
        }

        wasPlaying = isPlaying;
    }

    void WaterFall::updateTimestamps() {
        std::chrono::steady_clock::time_point newestTime;
        std::chrono::steady_clock::time_point oldestVisibleTime;
        {
            std::lock_guard<std::mutex> lck(timestampMtx);

            // Need at least 2 pts to calc duration
            if (fftTimestamps.size() < 2) { return; }
            newestTime = fftTimestamps.front();
            oldestVisibleTime = fftTimestamps.back();
        }

        const auto now = std::chrono::steady_clock::now();

        // Update 10 times/sec
        const auto SCALE_UPDATE_INTERVAL = std::chrono::milliseconds(100);
        if (now - lastScaleUpdateTime > SCALE_UPDATE_INTERVAL) {
            const auto visibleDuration = newestTime - oldestVisibleTime;
            const float actualTotalTimeMs = std::chrono::duration_cast<std::chrono::microseconds>(visibleDuration).count() / 1000.0f;

            if (actualTotalTimeMs > 0) {
                const float impliedTotalTimeMs = (float)waterfallHeight / currentPixelsPerMs;
                const float error = std::abs(actualTotalTimeMs - impliedTotalTimeMs) / impliedTotalTimeMs;

                // Snap if error is significant
                const float SCALE_UPDATE_THRESHOLD = 0.05f;
                if (error > SCALE_UPDATE_THRESHOLD) {
                    currentPixelsPerMs = (float)waterfallHeight / actualTotalTimeMs;
                }
            }

            lastScaleUpdateTime = now;
        }
    }

    bool WaterFall::uploadPaletteTexture() {
        if (!paletteDirty) { return false; }
        glBindTexture(GL_TEXTURE_2D, wfPaletteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, WATERFALL_RESOLUTION, 1, 0, GL_RGB, GL_FLOAT, paletteBuffer);
        paletteDirty = false;
        return true;
    }

    bool WaterFall::uploadPendingDeltas() {
        std::lock_guard<std::recursive_mutex> lck(latestFFTMtx);

        if (lastUploadedLine == currentFFTLine) {
            return false;
        }

        int safetyCounter = 0;
        int maxLines = waterfallHeight;

        while (lastUploadedLine != currentFFTLine && safetyCounter++ < maxLines) {
            lastUploadedLine--;
            if (lastUploadedLine < 0) {
                // Wrap around
                lastUploadedLine = waterfallHeight - 1;
            }

            uint8_t* srcLine = &rawFFTs[lastUploadedLine * dataWidth];
            Gfx_UpdateTexturePart(
                wfRawDataTexture,
                0, lastUploadedLine, // x, y
                dataWidth, 1,        // w, h
                GL_RED, GL_UNSIGNED_BYTE,
                srcLine,
                dataWidth);
        }

        return true;
    }

    void WaterFall::renderWaterfallTexture() {
        if (!waterfallVisible || waterfallHeight <= 0 || dataWidth <= 0 || !rawFFTs) { return; }

        bool needsDraw;

        if (uploadPendingDeltas()) {
            needsDraw = true;
        }
        else {
            needsDraw = waterfallUpdate;
        }

        if (uploadPaletteTexture()) {
            needsDraw = true;
        }

        if (!needsDraw) {
            // Nothing changed
            return;
        }

        Gfx_ScopedState stateSaver;

        // FBO Render
        glBindFramebuffer(GL_FRAMEBUFFER, wfFbo);
        glViewport(0, 0, dataWidth, waterfallHeight);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);

        glUseProgram(wfShaderProgram);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, wfRawDataTexture);
        Gfx_SetUniform1i(wfShaderProgram, "rawDataTex", 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, wfPaletteTexture);
        Gfx_SetUniform1i(wfShaderProgram, "paletteTex", 1);

        float offset = (float)currentFFTLine / (float)waterfallHeight;
        Gfx_SetUniform1f(wfShaderProgram, "uOffset", offset);

        glBindVertexArray(wfVao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        waterfallUpdate = false;
    }

    void WaterFall::drawWaterfall() {
        fixupTimestamps();
        updateTimestamps();

        ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        float textVOffset = 10.0f * style::uiScale;
        char buf[128];
        const int bufLen = (int)sizeof(buf);

        float totalVisibleTimeMs = (float)waterfallHeight / currentPixelsPerMs;
        double timeStepMs = findBestTimeRange(totalVisibleTimeMs, maxVerticalWfSteps);

        float scaleTickOfsset = 7 * style::uiScale;
        for (double timeAgoMs = timeStepMs;; timeAgoMs += timeStepMs) {
            float yPos = wfAreaMin.y + (timeAgoMs * currentPixelsPerMs);
            if (yPos > wfAreaMax.y) { break; }
            window->DrawList->AddLine(ImVec2(wfAreaMin.x, roundf(yPos)),
                                      ImVec2(wfAreaMin.x - scaleTickOfsset, roundf(yPos)),
                                      text, style::uiScale);
            const int textLen = printScaledTime(timeAgoMs, buf, bufLen);
            ImVec2 txtSz = ImGui::CalcTextSize(buf, &buf[textLen]);
            window->DrawList->AddText(ImVec2(wfAreaMin.x - txtSz.x - textVOffset, roundf(yPos - (txtSz.y / 2.0))), text, buf, &buf[textLen]);
        }

        renderWaterfallTexture();

        const ImVec2 drawMin = ImVec2(roundf(wfAreaMin.x), roundf(wfAreaMin.y));
        const ImVec2 drawMax = ImVec2(roundf(wfAreaMax.x), roundf(wfAreaMax.y));

        window->DrawList->AddImage((void*)(intptr_t)wfFboTexture, drawMin, drawMax,
                                   ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

        ImVec2 mPos = ImGui::GetMousePos();

        if (!gui::mainWindow.processMouseInputs && !inputHandled && IS_IN_AREA(mPos, wfAreaMin, wfAreaMax)) {
            for (auto const& [name, vfo] : vfos) {
                window->DrawList->AddRectFilled(vfo->wfRectMin, vfo->wfRectMax, vfo->color);
                if (!vfo->lineVisible) { continue; }
                window->DrawList->AddLine(vfo->wfLineMin, vfo->wfLineMax, (name == selectedVFO) ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 255, 0, 255), style::uiScale);
            }
        }

        if (gui::mainWindow.processMouseInputs) {
            const ImRect wfRect(wfAreaMin, wfAreaMax);

            if (wfRect.Contains(ImGui::GetIO().MousePos)) {
                const float flashSpeed = 10.0f;
                wfCursorFlashAccum += ImGui::GetIO().DeltaTime;

                // NOTE: using a single color on the waterfall display might be
                // hard to see so alternate between black and white.
                const float t = (sinf(wfCursorFlashAccum * flashSpeed) * 0.5f) + 0.5f;
                const int intensity = (int)(t * 255.0f);

                const ImU32 color = IM_COL32(intensity, intensity, intensity, 255);
                DrawCrosshairUnderCursor(ImRect(wfAreaMin, wfAreaMax), color, 1.0f, ImGuiCrosshairFlags_CullVertical);
            }
        }
    }

    void WaterFall::drawVFOs() {
        for (auto const& [name, vfo] : vfos) {
            vfo->draw(window, name == selectedVFO);
        }
    }

    void WaterFall::selectFirstVFO() {
        bool available = false;
        for (auto const& [name, vfo] : vfos) {
            available = true;
            selectedVFO = name;
            selectedVFOChanged = true;
            return;
        }
        if (!available) {
            selectedVFO.clear();
            selectedVFOChanged = true;
        }
    }

    void WaterFall::processInputs() {
        // Pre calculate useful values
        WaterfallVFO* selVfo = NULL;
        if (!selectedVFO.empty()) {
            selVfo = vfos[selectedVFO];
        }

        bool mouseHovered, mouseHeld;
        bool mouseClicked = ImGui::ButtonBehavior(ImRect(fftAreaMin, wfAreaMax), GetID("WaterfallID"), &mouseHovered, &mouseHeld,
                                                  ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_PressedOnClick);

        ImVec2 mousePos = ImGui::GetMousePos();

        if (mouseClicked) {
            dragStartPos = mousePos;
            isMouseDragging = true;
        }

        ImVec2 drag = mousePos - dragStartPos;
        ImVec2 dragOrigin = isMouseDragging ? dragStartPos : mousePos;

        mouseInFFTResize = (dragOrigin.x > widgetPos.x && dragOrigin.x < widgetPos.x + widgetSize.x && dragOrigin.y >= widgetPos.y + newFFTAreaHeight - (2.0f * style::uiScale) && dragOrigin.y <= widgetPos.y + newFFTAreaHeight + (2.0f * style::uiScale));
        mouseInFreq = IS_IN_AREA(dragOrigin, freqAreaMin, freqAreaMax);
        if (mouseClicked && !doCursorWarp) {
            doCursorWarp = mouseInFreq;
        }
        mouseInFFT = IS_IN_AREA(dragOrigin, fftAreaMin, fftAreaMax);
        mouseInWaterfall = IS_IN_AREA(dragOrigin, wfAreaMin, wfAreaMax);

        int mouseWheel = ImGui::GetIO().MouseWheel + ImGui::GetIO().MouseWheelH;

        bool mouseMoved = false;
        if (mousePos.x != lastMousePos.x || mousePos.y != lastMousePos.y) { mouseMoved = true; }
        lastMousePos = mousePos;

        const std::string* hoveredVFOName = nullptr;
        for (auto const& [name, _vfo] : vfos) {
            if (ImGui::IsMouseHoveringRect(_vfo->rectMin, _vfo->rectMax) || ImGui::IsMouseHoveringRect(_vfo->wfRectMin, _vfo->wfRectMax)) {
                hoveredVFOName = &name;
                break;
            }
        }

        const bool mouseLeftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

        // Deselect everything if the mouse is released
        if (!mouseLeftDown) {
            if (fftResizeSelect) {
                FFTAreaHeight = newFFTAreaHeight;
                onResize();
            }

            fftResizeSelect = false;
            freqScaleSelect = false;
            vfoBorderSelect = false;
            isMouseDragging = false;
            lastDrag = 0;
            dragStartPos = ImVec2(0, 0);
            crosshairFlags = ImGuiCrosshairFlags_None;
        }

        bool targetFound = false;

        // If the mouse was clicked anywhere in the waterfall, check if the resize was clicked
        if (mouseInFFTResize) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                fftResizeSelect = true;
                doCursorWarp = false;
                targetFound = true;
            }
        }

        // If mouse was clicked inside the central part, check what was clicked
        if (mouseClicked && !targetFound) {
            mouseDownPos = mousePos;

            // First, check if a VFO border was selected
            for (auto const& [name, _vfo] : vfos) {
                if (_vfo->bandwidthLocked) { continue; }
                if (_vfo->rectMax.x - _vfo->rectMin.x < 10) { continue; }
                bool resizing = false;
                if (_vfo->reference != REF_LOWER) {
                    if (IS_IN_AREA(mousePos, _vfo->lbwSelMin, _vfo->lbwSelMax)) { resizing = true; }
                    else if (IS_IN_AREA(mousePos, _vfo->wfLbwSelMin, _vfo->wfLbwSelMax)) {
                        resizing = true;
                    }
                }
                if (_vfo->reference != REF_UPPER) {
                    if (IS_IN_AREA(mousePos, _vfo->rbwSelMin, _vfo->rbwSelMax)) { resizing = true; }
                    else if (IS_IN_AREA(mousePos, _vfo->wfRbwSelMin, _vfo->wfRbwSelMax)) {
                        resizing = true;
                    }
                }
                if (!resizing) { continue; }
                relatedVfo = _vfo;
                vfoBorderSelect = true;
                targetFound = true;
                break;
            }

            // Next, check if a VFO was selected
            if (!targetFound && hoveredVFOName) {
                selectedVFO = *hoveredVFOName;
                selectedVFOChanged = true;
                targetFound = true;
                return;
            }

            // Now, check frequency scale
            if (!targetFound && mouseInFreq) {
                freqScaleSelect = true;
            }
        }

        // If the FFT resize bar was selected, resize FFT accordingly
        if (fftResizeSelect) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            newFFTAreaHeight = mousePos.y - widgetPos.y;
            newFFTAreaHeight = std::clamp<float>(newFFTAreaHeight, 150, widgetSize.y - 50);
            ImGui::GetForegroundDrawList()->AddLine(ImVec2(widgetPos.x, newFFTAreaHeight + widgetPos.y), ImVec2(widgetEndPos.x, newFFTAreaHeight + widgetPos.y),
                                                    ImGui::GetColorU32(ImGuiCol_SeparatorActive), style::uiScale);
            return;
        }

        // If a vfo border is selected, resize VFO accordingly
        if (vfoBorderSelect) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            double dist = (relatedVfo->reference == REF_CENTER) ? fabsf(mousePos.x - relatedVfo->lineMin.x) : (mousePos.x - relatedVfo->lineMin.x);
            if (relatedVfo->reference == REF_UPPER) { dist = -dist; }
            double hzDist = dist * (viewBandwidth / (double)dataWidth);
            if (relatedVfo->reference == REF_CENTER) {
                hzDist *= 2.0;
            }
            hzDist = std::clamp<double>(hzDist, relatedVfo->minBandwidth, relatedVfo->maxBandwidth);
            relatedVfo->setBandwidth(hzDist);
            relatedVfo->onUserChangedBandwidth.emit(hzDist);

            // Avoid confusion with the vfo line when moving
            crosshairFlags |= ImGuiCrosshairFlags_CullVertical;
            return;
        }

        // If the frequency scale is selected, move it
        if (freqScaleSelect) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            double deltax = drag.x - lastDrag;
            lastDrag = drag.x;
            double viewDelta = deltax * (viewBandwidth / (double)dataWidth);

            viewOffset -= viewDelta;

            if (viewOffset + (viewBandwidth / 2.0) > wholeBandwidth / 2.0) {
                double freqOffset = (viewOffset + (viewBandwidth / 2.0)) - (wholeBandwidth / 2.0);
                viewOffset = (wholeBandwidth / 2.0) - (viewBandwidth / 2.0);
                if (!centerFrequencyLocked) {
                    centerFreq += freqOffset;
                    centerFreqMoved = true;
                }
            }
            if (viewOffset - (viewBandwidth / 2.0) < -(wholeBandwidth / 2.0)) {
                double freqOffset = (viewOffset - (viewBandwidth / 2.0)) + (wholeBandwidth / 2.0);
                viewOffset = (viewBandwidth / 2.0) - (wholeBandwidth / 2.0);
                if (!centerFrequencyLocked) {
                    centerFreq += freqOffset;
                    centerFreqMoved = true;
                }
            }

            lowerFreq = (centerFreq + viewOffset) - (viewBandwidth / 2.0);
            upperFreq = (centerFreq + viewOffset) + (viewBandwidth / 2.0);

            if (viewBandwidth != wholeBandwidth) {
                updateAllVFOs();
                if (_fullUpdate) { waterfallUpdate = true; };
            }
            return;
        }

        // If the mouse wheel is moved on the frequency scale
        if (mouseWheel != 0 && mouseInFreq) {
            viewOffset -= (double)mouseWheel * viewBandwidth / 20.0;

            if (viewOffset + (viewBandwidth / 2.0) > wholeBandwidth / 2.0) {
                double freqOffset = (viewOffset + (viewBandwidth / 2.0)) - (wholeBandwidth / 2.0);
                viewOffset = (wholeBandwidth / 2.0) - (viewBandwidth / 2.0);
                centerFreq += freqOffset;
                centerFreqMoved = true;
            }
            if (viewOffset - (viewBandwidth / 2.0) < -(wholeBandwidth / 2.0)) {
                double freqOffset = (viewOffset - (viewBandwidth / 2.0)) + (wholeBandwidth / 2.0);
                viewOffset = (viewBandwidth / 2.0) - (wholeBandwidth / 2.0);
                centerFreq += freqOffset;
                centerFreqMoved = true;
            }

            lowerFreq = (centerFreq + viewOffset) - (viewBandwidth / 2.0);
            upperFreq = (centerFreq + viewOffset) + (viewBandwidth / 2.0);

            if (viewBandwidth != wholeBandwidth) {
                updateAllVFOs();
                if (_fullUpdate) { waterfallUpdate = true; };
            }
            return;
        }

        // If the left and right keys are pressed while hovering the freq scale, move it too
        if (mouseInFreq) {
            bool leftKeyPressed = ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
            if (leftKeyPressed || ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                viewOffset += leftKeyPressed ? (viewBandwidth / 20.0) : (-viewBandwidth / 20.0);

                if (viewOffset + (viewBandwidth / 2.0) > wholeBandwidth / 2.0) {
                    double freqOffset = (viewOffset + (viewBandwidth / 2.0)) - (wholeBandwidth / 2.0);
                    viewOffset = (wholeBandwidth / 2.0) - (viewBandwidth / 2.0);
                    centerFreq += freqOffset;
                    centerFreqMoved = true;
                }
                if (viewOffset - (viewBandwidth / 2.0) < -(wholeBandwidth / 2.0)) {
                    double freqOffset = (viewOffset - (viewBandwidth / 2.0)) + (wholeBandwidth / 2.0);
                    viewOffset = (viewBandwidth / 2.0) - (wholeBandwidth / 2.0);
                    centerFreq += freqOffset;
                    centerFreqMoved = true;
                }

                lowerFreq = (centerFreq + viewOffset) - (viewBandwidth / 2.0);
                upperFreq = (centerFreq + viewOffset) + (viewBandwidth / 2.0);

                if (viewBandwidth != wholeBandwidth) {
                    updateAllVFOs();
                    if (_fullUpdate) { waterfallUpdate = true; };
                }
                return;
            }
        }

        // Finally, if nothing else was selected, just move the VFO
        if ((VFOMoveSingleClick ? ImGui::IsMouseClicked(ImGuiMouseButton_Left) : mouseLeftDown) && (mouseInFFT || mouseInWaterfall) && (mouseMoved || hoveredVFOName)) {
            if (selVfo != NULL) {
                int refCenter = mousePos.x - fftAreaMin.x;
                if (refCenter >= 0 && refCenter < dataWidth) {
                    double off = ((((double)refCenter / ((double)dataWidth / 2.0)) - 1.0) * (viewBandwidth / 2.0)) + viewOffset;
                    off += centerFreq;
                    off = (round(off / selVfo->snapInterval) * selVfo->snapInterval) - centerFreq;
                    selVfo->setOffset(off);

                    // Avoid confusion with the vfo line when moving
                    crosshairFlags |= ImGuiCrosshairFlags_CullVertical;
                }
            }
        }
        else if (!mouseLeftDown) {
            // Check if a VFO is hovered. If yes, show tooltip
            for (auto const& [name, _vfo] : vfos) {
                if (ImGui::IsMouseHoveringRect(_vfo->rectMin, _vfo->rectMax) || ImGui::IsMouseHoveringRect(_vfo->wfRectMin, _vfo->wfRectMax)) {
                    char buf[128];
                    const int bufLen = (int)sizeof(buf);
                    ImGui::BeginTooltip();

                    ImGui::TextUnformatted(name.c_str(), &name[name.length()]);

                    if (ImGui::GetIO().KeyCtrl) {
                        ImGui::Separator();
                        printScaledFrequency(_vfo->generalOffset + centerFreq, buf, bufLen);
                        ImGui::Text("Frequency: %s", buf);
                        printScaledFrequency(_vfo->bandwidth, buf, bufLen);
                        ImGui::Text("Bandwidth: %s", buf);
                        ImGui::Text("Bandwidth Locked: %s", _vfo->bandwidthLocked ? "Yes" : "No");

                        float strength, snr;
                        if (calculateVFOSignalInfo(latestFFT, _vfo, strength, snr)) {
                            ImGui::Text("Strength: %0.1fdBFS", strength);
                            ImGui::Text("SNR: %0.1fdB", snr);
                        }
                        else {
                            ImGui::TextUnformatted("Strength: ---.-dBFS");
                            ImGui::TextUnformatted("SNR: ---.-dB");
                        }
                    }

                    ImGui::EndTooltip();
                    break;
                }
            }
        }

        // Handle Page Up to cycle through VFOs
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp) && selVfo != NULL) {
            const std::string* next = NULL;
            const std::string* lowest = NULL;
            double lowestOffset = INFINITY;
            double smallestDistance = INFINITY;
            double firstVfoOffset = selVfo->generalOffset;
            for (const auto& [name, vfo] : vfos) {
                if (vfo->generalOffset > firstVfoOffset) {
                    double dist = vfo->generalOffset - firstVfoOffset;
                    if (dist < smallestDistance) {
                        smallestDistance = dist;
                        next = &name;
                    }
                }
                if (vfo->generalOffset < lowestOffset) {
                    lowestOffset = vfo->generalOffset;
                    lowest = &name;
                }
            }
            if (next) {
                selectedVFO = *next;
            }
            else if (lowest) {
                selectedVFO = *lowest;
            }
            selectedVFOChanged = true;
        }

        // Handle Page Down to cycle through VFOs
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown) && selVfo != NULL) {
            const std::string* next = NULL;
            const std::string* highest = NULL;
            double highestOffset = -INFINITY;
            double smallestDistance = INFINITY;
            double currentOffset = selVfo->generalOffset;
            for (const auto& [name, vfo] : vfos) {
                if (vfo->generalOffset < currentOffset) {
                    double dist = currentOffset - vfo->generalOffset;
                    if (dist < smallestDistance) {
                        smallestDistance = dist;
                        next = &name;
                    }
                }
                if (vfo->generalOffset > highestOffset) {
                    highestOffset = vfo->generalOffset;
                    highest = &name;
                }
            }
            if (next) {
                selectedVFO = *next;
            }
            else if (highest) {
                selectedVFO = *highest;
            }
            selectedVFOChanged = true;
        }
    }

    bool WaterFall::calculateVFOSignalInfo(float* fftLine, WaterfallVFO* _vfo, float& strength, float& snr) {
        if (fftLine == NULL || fftLines <= 0) { return false; }

        // Calculate FFT index data
        double vfoMinSizeFreq = _vfo->centerOffset - _vfo->bandwidth;
        double vfoMinFreq = _vfo->centerOffset - (_vfo->bandwidth / 2.0);
        double vfoMaxFreq = _vfo->centerOffset + (_vfo->bandwidth / 2.0);
        double vfoMaxSizeFreq = _vfo->centerOffset + _vfo->bandwidth;
        int vfoMinSideOffset = std::clamp<int>(((vfoMinSizeFreq / (wholeBandwidth / 2.0)) * (double)(rawFFTSize / 2)) + (rawFFTSize / 2), 0, rawFFTSize);
        int vfoMinOffset = std::clamp<int>(((vfoMinFreq / (wholeBandwidth / 2.0)) * (double)(rawFFTSize / 2)) + (rawFFTSize / 2), 0, rawFFTSize);
        int vfoMaxOffset = std::clamp<int>(((vfoMaxFreq / (wholeBandwidth / 2.0)) * (double)(rawFFTSize / 2)) + (rawFFTSize / 2), 0, rawFFTSize);
        int vfoMaxSideOffset = std::clamp<int>(((vfoMaxSizeFreq / (wholeBandwidth / 2.0)) * (double)(rawFFTSize / 2)) + (rawFFTSize / 2), 0, rawFFTSize);

        double avg = 0;
        float max = -INFINITY;
        int avgCount = 0;

        // Calculate Left average
        for (int i = vfoMinSideOffset; i < vfoMinOffset; i++) {
            avg += fftLine[i];
            avgCount++;
        }

        // Calculate Right average
        for (int i = vfoMaxOffset + 1; i < vfoMaxSideOffset; i++) {
            avg += fftLine[i];
            avgCount++;
        }

        avg /= (double)(avgCount);

        // Calculate max
        for (int i = vfoMinOffset; i <= vfoMaxOffset; i++) {
            if (fftLine[i] > max) { max = fftLine[i]; }
        }

        strength = max;
        snr = max - avg;

        return true;
    }

    static void doTrimTimestamps(std::deque<std::chrono::steady_clock::time_point>& timeStamps, const int waterfallHeight) {
        // Trim the timestamps if waterfall has shrunk
        if (timeStamps.size() > waterfallHeight) {
            timeStamps.resize(waterfallHeight);
        }
    }

    void WaterFall::addTimestamp() {
        std::lock_guard<std::mutex> lck(timestampMtx);
        fftTimestamps.push_front(std::chrono::steady_clock::now());
        doTrimTimestamps(fftTimestamps, waterfallHeight);
    }

    void WaterFall::trimTimestamps() {
        std::lock_guard<std::mutex> lck(timestampMtx);
        doTrimTimestamps(fftTimestamps, waterfallHeight);
    }

    void WaterFall::drawBandPlan() {
        int count = bandplan->bands.size();
        double horizScale = (double)dataWidth / viewBandwidth;
        double start, end, center, aPos, bPos, cPos, width;
        ImVec2 txtSz;
        bool startVis, endVis;
        uint32_t color, colorTrans;

        float height = ImGui::CalcTextSize("0").y * 2.5f;
        float bpBottom;

        if (bandPlanPos == BANDPLAN_POS_BOTTOM) {
            bpBottom = fftAreaMax.y;
        }
        else {
            bpBottom = fftAreaMin.y + height + 1;
        }


        for (int i = 0; i < count; i++) {
            start = bandplan->bands[i].start;
            end = bandplan->bands[i].end;
            if (start < lowerFreq && end < lowerFreq) {
                continue;
            }
            if (start > upperFreq && end > upperFreq) {
                continue;
            }
            startVis = (start > lowerFreq);
            endVis = (end < upperFreq);
            start = std::clamp<double>(start, lowerFreq, upperFreq);
            end = std::clamp<double>(end, lowerFreq, upperFreq);
            center = (start + end) / 2.0;
            aPos = fftAreaMin.x + ((start - lowerFreq) * horizScale);
            bPos = fftAreaMin.x + ((end - lowerFreq) * horizScale);
            cPos = fftAreaMin.x + ((center - lowerFreq) * horizScale);
            width = bPos - aPos;
            const std::string& bandName = bandplan->bands[i].name;
            txtSz = ImGui::CalcTextSize(bandName.c_str(), &bandName[bandName.length()]);
            if (bandplan::colorTable.find(bandplan->bands[i].type) != bandplan::colorTable.end()) {
                color = bandplan::colorTable[bandplan->bands[i].type].colorValue;
                colorTrans = bandplan::colorTable[bandplan->bands[i].type].transColorValue;
            }
            else {
                color = IM_COL32(255, 255, 255, 255);
                colorTrans = IM_COL32(255, 255, 255, 100);
            }
            if (aPos <= fftAreaMin.x) {
                aPos = fftAreaMin.x + 1;
            }
            if (bPos <= fftAreaMin.x) {
                bPos = fftAreaMin.x + 1;
            }
            if (width >= 1.0) {
                window->DrawList->AddRectFilled(ImVec2(roundf(aPos), bpBottom - height),
                                                ImVec2(roundf(bPos), bpBottom), colorTrans);
                if (startVis) {
                    window->DrawList->AddLine(ImVec2(roundf(aPos), bpBottom - height - 1),
                                              ImVec2(roundf(aPos), bpBottom - 1), color, style::uiScale);
                }
                if (endVis) {
                    window->DrawList->AddLine(ImVec2(roundf(bPos), bpBottom - height - 1),
                                              ImVec2(roundf(bPos), bpBottom - 1), color, style::uiScale);
                }
            }
            if (txtSz.x <= width) {
                window->DrawList->AddText(ImVec2(cPos - (txtSz.x / 2.0), bpBottom - (height / 2.0f) - (txtSz.y / 2.0f)),
                                          IM_COL32(255, 255, 255, 255), bandName.c_str(), &bandName[bandName.length()]);
            }
        }
    }

    void WaterFall::updateWidgetPositions() {
        fftAreaMin = ImVec2(widgetPos.x + (80.0f * style::uiScale), widgetPos.y + (9.0f * style::uiScale));
        fftAreaMax = ImVec2(fftAreaMin.x + dataWidth, fftAreaMin.y + fftHeight + 1);

        freqAreaMin = ImVec2(fftAreaMin.x, fftAreaMax.y + 1);
        freqAreaMax = ImVec2(fftAreaMax.x, fftAreaMax.y + (40.0f * style::uiScale));

        wfAreaMin = ImVec2(fftAreaMin.x, freqAreaMax.y + 1);
        wfAreaMax = ImVec2(fftAreaMin.x + dataWidth, wfAreaMin.y + waterfallHeight);
    }

    void WaterFall::onPositionChange() {
        // NEW: since we upgraded to ImGui docking, this needs to be
        // performed as well in order to keep the waterfall display
        // in view during position changed!
        updateWidgetPositions();
    }

    void WaterFall::onResize() {
        std::lock_guard<std::recursive_mutex> lck(latestFFTMtx);
        std::lock_guard<std::mutex> lck2(smoothingBufMtx);

        if (widgetSize.x < 100 || widgetSize.y < 100) {
            return;
        }

        int oldDataWidth = dataWidth;
        int oldWaterfallHeight = waterfallHeight;
        int oldHead = currentFFTLine;
        uint8_t* oldRawFFTs = rawFFTs; // Old buf ptr

        if (waterfallVisible) {
            FFTAreaHeight = std::min<int>(FFTAreaHeight, widgetSize.y - (50.0f * style::uiScale));
            newFFTAreaHeight = FFTAreaHeight;
            fftHeight = FFTAreaHeight - (50.0f * style::uiScale);
            waterfallHeight = widgetSize.y - fftHeight - (50.0f * style::uiScale) - 2;
        }
        else {
            fftHeight = widgetSize.y - (50.0f * style::uiScale);
        }
        dataWidth = widgetSize.x - (90.0f * style::uiScale);

        if (waterfallVisible) {
            fftLines = 0;
            currentFFTLine = 0;
            lastUploadedLine = 0;

            rawFFTs = (uint8_t*)malloc(waterfallHeight * dataWidth * sizeof(uint8_t));
            memset(rawFFTs, 0, waterfallHeight * dataWidth * sizeof(uint8_t));

            // Migrate data
            if (oldRawFFTs && oldWaterfallHeight > 0 && oldDataWidth > 0) {
                const int linesToCopy = std::min<int>(waterfallHeight, oldWaterfallHeight);
                const float scale = (float)oldDataWidth / (float)dataWidth;

                for (int i = 0; i < linesToCopy; i++) {
                    const int srcIndex = (oldHead + i) % oldWaterfallHeight;
                    uint8_t* srcRow = &oldRawFFTs[srcIndex * oldDataWidth];
                    uint8_t* dstRow = &rawFFTs[i * dataWidth];

                    // Resample horizontal
                    for (int x = 0; x < dataWidth; x++) {
                        const int srcX = (int)((x + 0.5f) * scale);
                        dstRow[x] = srcRow[std::clamp(srcX, 0, oldDataWidth - 1)];
                    }
                }
            }

            if (oldRawFFTs) {
                free(oldRawFFTs);
                oldRawFFTs = NULL;
            }

            // Update texture
            glBindTexture(GL_TEXTURE_2D, wfFboTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dataWidth, waterfallHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

            glBindTexture(GL_TEXTURE_2D, wfRawDataTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, dataWidth, waterfallHeight, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);

            GLint prevAlign;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);

            // 1 byte align if width is not mul4
            const bool tightAlign = (dataWidth & 3) != 0;
            if (tightAlign) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            }

            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, dataWidth, waterfallHeight, GL_RED, GL_UNSIGNED_BYTE, rawFFTs);
            
            // Restore
            if (tightAlign && prevAlign != 1) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, wfFbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, wfFboTexture, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                flog::error("Waterfall FBO not complete!");
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            trimTimestamps();
            waterfallUpdate = true;
        }
        else {
            // If waterfall became invisible, just free mem
            if (oldRawFFTs) { free(oldRawFFTs); }
            rawFFTs = NULL;
        }

        // Reallocate display FFT
        if (latestFFT != NULL) {
            delete[] latestFFT;
        }
        latestFFT = new float[dataWidth];

        // Reallocate hold FFT
        if (latestFFTHold != NULL) {
            delete[] latestFFTHold;
        }
        latestFFTHold = new float[dataWidth];

        // Reallocate temporary buffer for zooming the FFT
        if (tempZoomFFT != NULL) {
            delete[] tempZoomFFT;
        }
        tempZoomFFT = new float[dataWidth];

        // Reallocate smoothing buffer
        if (fftSmoothing) {
            if (smoothingBuf) { delete[] smoothingBuf; }
            smoothingBuf = new float[dataWidth];
            for (int i = 0; i < dataWidth; i++) {
                smoothingBuf[i] = -1000.0f;
            }
        }

        for (int i = 0; i < dataWidth; i++) {
            latestFFT[i] = -1000.0f; // Hide everything
            latestFFTHold[i] = -1000.0f;
        }

        updateWidgetPositions();
        const ImVec2 targetTextSize = ImGui::CalcTextSize("000.000");

        maxHorizontalSteps = dataWidth / (targetTextSize.x + 10);
        maxVerticalFftSteps = fftHeight / targetTextSize.y;
        maxVerticalWfSteps = (waterfallHeight / targetTextSize.y) - 1; // -1 because first step will clip.

        range = findBestFrequencyRange(viewBandwidth, maxHorizontalSteps);
        verticalFftRange = findBestFrequencyRange(fftMax - fftMin, maxVerticalFftSteps);

        waterfallUpdate = true;
        updateAllVFOs();
    }

    void WaterFall::draw() {
        if (doCursorWarp && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImGuiContext& g = *GImGui;
            ImVec2 oldMousePos = g.IO.MousePos;

            if (ImGui::WrapMousePosEx(ImGuiWrapMouseFlags_Both, ImGui::GetCurrentWindow()->Rect())) {
                dragStartPos += (g.IO.MousePos - oldMousePos);
            }
        }

        buf_mtx.lock();
        window = GetCurrentWindow();

        widgetPos = ImGui::GetWindowContentRegionMin();
        widgetEndPos = ImGui::GetWindowContentRegionMax();
        widgetPos.x += window->Pos.x;
        widgetPos.y += window->Pos.y;
        widgetEndPos.x += window->Pos.x;
        widgetEndPos.y += window->Pos.y;
        widgetSize = ImVec2(widgetEndPos.x - widgetPos.x, widgetEndPos.y - widgetPos.y);

        if (selectedVFO.empty() && !vfos.empty()) {
            selectFirstVFO();
        }

        if (widgetPos.x != lastWidgetPos.x || widgetPos.y != lastWidgetPos.y) {
            lastWidgetPos = widgetPos;
            onPositionChange();
        }
        if (widgetSize.x != lastWidgetSize.x || widgetSize.y != lastWidgetSize.y) {
            lastWidgetSize = widgetSize;
            onResize();
        }

        //window->DrawList->AddRectFilled(widgetPos, widgetEndPos, IM_COL32( 0, 0, 0, 255 ));
        ImU32 bg = ImGui::ColorConvertFloat4ToU32(gui::themeManager.getCoreColor(ThemeManager::CoreCol_WaterfallBg));
        window->DrawList->AddRectFilled(widgetPos, widgetEndPos, bg, 3.0f * style::uiScale);

        if (gui::mainWindow.processMouseInputs && gui::mainWindow.processKeyboardInputs) {
            inputHandled = false;
            InputHandlerArgs args;
            args.fftRectMin = fftAreaMin;
            args.fftRectMax = fftAreaMax;
            args.freqScaleRectMin = freqAreaMin;
            args.freqScaleRectMax = freqAreaMax;
            args.waterfallRectMin = wfAreaMin;
            args.waterfallRectMax = wfAreaMax;
            args.lowFreq = lowerFreq;
            args.highFreq = upperFreq;
            args.freqToPixelRatio = (double)dataWidth / viewBandwidth;
            args.pixelToFreqRatio = viewBandwidth / (double)dataWidth;
            onInputProcess.emit(args);
            if (!inputHandled) {
                processInputs();
            }

            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                doCursorWarp = false;
            }
        }

        updateAllVFOs(true);

        drawFFT();
        if (waterfallVisible) {
            drawWaterfall();
        }
        drawVFOs();
        if (bandplan != NULL && bandplanVisible) {
            drawBandPlan();
        }

        const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(gui::themeManager.getCoreColor(ThemeManager::CoreCol_WaterfallSeparatorColor));
        window->DrawList->AddLine(ImVec2(widgetPos.x, wfAreaMin.y), ImVec2(widgetPos.x + widgetSize.x, wfAreaMin.y), lineColor, style::uiScale);

        const ImU32 rectColor = ImGui::ColorConvertFloat4ToU32(gui::themeManager.getCoreColor(ThemeManager::CoreCol_MainBorderColor));
        window->DrawList->AddRect(widgetPos, widgetEndPos, rectColor, 1.0f * style::uiScale, 0, style::uiScale);

        buf_mtx.unlock();
    }

    float* WaterFall::getFFTBuffer() {
        buf_mtx.lock();
        if (inputBuffer == NULL) {
            buf_mtx.unlock();
            return NULL;
        }
        return inputBuffer;
    }

    void WaterFall::pushFFT() {
        if (inputBuffer == NULL) {
            buf_mtx.unlock();
            return;
        }
        addTimestamp();
        std::lock_guard<std::recursive_mutex> lck(latestFFTMtx);

        double offsetRatio = viewOffset / (wholeBandwidth / 2.0);
        int drawDataSize = (viewBandwidth / wholeBandwidth) * rawFFTSize;
        int drawDataStart = (((double)rawFFTSize / 2.0) * (offsetRatio + 1)) - (drawDataSize / 2);

        doZoom(drawDataStart, drawDataSize, rawFFTSize, dataWidth, inputBuffer, latestFFT);

        if (waterfallVisible) {
            // Note: stuff is filled backwards!
            currentFFTLine--;
            fftLines++;

            currentFFTLine = ((currentFFTLine + waterfallHeight) % waterfallHeight);
            fftLines = std::min<float>(fftLines, waterfallHeight);

            if (rawFFTs) {
                // Bake min/max
                const float range = waterfallMax - waterfallMin;
                assert(range > 0.0f);
                const float scale = 255.0f / range;
                uint8_t* destLine = &rawFFTs[currentFFTLine * dataWidth];

                for (int i = 0; i < dataWidth; i++) {
                    float val = latestFFT[i];
                    float normalized = (val - waterfallMin) * scale;
                    destLine[i] = (uint8_t)std::clamp(normalized, 0.0f, 255.0f);
                }
            }
            waterfallUpdate = true;
        }
        else {
            fftLines = 1;
        }

        // Apply smoothing if enabled
        if (fftSmoothing && latestFFT != NULL && smoothingBuf != NULL && fftLines != 0) {
            std::lock_guard<std::mutex> lck2(smoothingBufMtx);
            volk_32f_s32f_multiply_32f(latestFFT, latestFFT, fftSmoothingAlpha, dataWidth);
            volk_32f_s32f_multiply_32f(smoothingBuf, smoothingBuf, fftSmoothingBeta, dataWidth);
            volk_32f_x2_add_32f(smoothingBuf, latestFFT, smoothingBuf, dataWidth);
            memcpy(latestFFT, smoothingBuf, dataWidth * sizeof(float));
        }

        if (!selectedVFO.empty() && !vfos.empty()) {
            float dummy;
            if (snrSmoothing) {
                float newSNR = 0.0f;
                // Use inputBuffer (highest resolution) for SNR calculation
                calculateVFOSignalInfo(inputBuffer, vfos[selectedVFO], dummy, newSNR);
                selectedVFOSNR = (snrSmoothingBeta * selectedVFOSNR) + (snrSmoothingAlpha * newSNR);
            }
            else {
                calculateVFOSignalInfo(inputBuffer, vfos[selectedVFO], dummy, selectedVFOSNR);
            }
        }

        // If FFT hold is enabled, update it
        if (fftHold && latestFFT != NULL && latestFFTHold != NULL && fftLines != 0) {
            for (int i = 1; i < dataWidth; i++) {
                latestFFTHold[i] = std::max<float>(latestFFT[i], latestFFTHold[i] - fftHoldSpeed);
            }
        }

        buf_mtx.unlock();
    }

    void WaterFall::updatePallette(const float* colors, int colorCount) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        for (int i = 0; i < WATERFALL_RESOLUTION; i++) {
            int lowerId = floorf(((float)i / (float)WATERFALL_RESOLUTION) * colorCount);
            int upperId = ceilf(((float)i / (float)WATERFALL_RESOLUTION) * colorCount);
            lowerId = std::clamp<int>(lowerId, 0, colorCount - 1);
            upperId = std::clamp<int>(upperId, 0, colorCount - 1);
            float ratio = (((float)i / (float)WATERFALL_RESOLUTION) * colorCount) - lowerId;
            float r = (colors[(lowerId * 3) + 0] * (1.0 - ratio)) + (colors[(upperId * 3) + 0] * (ratio));
            float g = (colors[(lowerId * 3) + 1] * (1.0 - ratio)) + (colors[(upperId * 3) + 1] * (ratio));
            float b = (colors[(lowerId * 3) + 2] * (1.0 - ratio)) + (colors[(upperId * 3) + 2] * (ratio));

            paletteBuffer[(i * 3) + 0] = r / 255.0f;
            paletteBuffer[(i * 3) + 1] = g / 255.0f;
            paletteBuffer[(i * 3) + 2] = b / 255.0f;
        }
        paletteDirty = true;
        waterfallUpdate = true;
    }

    void WaterFall::autoRange() {
        std::lock_guard<std::recursive_mutex> lck(latestFFTMtx);
        float min = INFINITY;
        float max = -INFINITY;
        for (int i = 0; i < dataWidth; i++) {
            if (latestFFT[i] < min) {
                min = latestFFT[i];
            }
            if (latestFFT[i] > max) {
                max = latestFFT[i];
            }
        }
        fftMin = min - 5;
        fftMax = max + 5;
    }

    void WaterFall::setCenterFrequency(double freq) {
        centerFreq = freq;
        lowerFreq = (centerFreq + viewOffset) - (viewBandwidth / 2.0);
        upperFreq = (centerFreq + viewOffset) + (viewBandwidth / 2.0);
        updateAllVFOs();
    }

    double WaterFall::getCenterFrequency() {
        return centerFreq;
    }

    void WaterFall::setBandwidth(double bandWidth) {
        double currentRatio = viewBandwidth / wholeBandwidth;
        wholeBandwidth = bandWidth;
        setViewBandwidth(bandWidth * currentRatio);
        for (auto const& [name, vfo] : vfos) {
            if (vfo->lowerOffset < -(bandWidth / 2)) {
                vfo->setCenterOffset(-(bandWidth / 2));
            }
            if (vfo->upperOffset > (bandWidth / 2)) {
                vfo->setCenterOffset(bandWidth / 2);
            }
        }
        updateAllVFOs();
    }

    double WaterFall::getBandwidth() {
        return wholeBandwidth;
    }

    void WaterFall::setViewBandwidth(double bandWidth) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        if (bandWidth == viewBandwidth) {
            return;
        }
        if (abs(viewOffset) + (bandWidth / 2.0) > wholeBandwidth / 2.0) {
            if (viewOffset < 0) {
                viewOffset = (bandWidth / 2.0) - (wholeBandwidth / 2.0);
            }
            else {
                viewOffset = (wholeBandwidth / 2.0) - (bandWidth / 2.0);
            }
        }
        viewBandwidth = bandWidth;
        lowerFreq = (centerFreq + viewOffset) - (viewBandwidth / 2.0);
        upperFreq = (centerFreq + viewOffset) + (viewBandwidth / 2.0);
        range = findBestFrequencyRange(bandWidth, maxHorizontalSteps);
        if (_fullUpdate) { waterfallUpdate = true; };
        updateAllVFOs();
    }

    double WaterFall::getViewBandwidth() {
        return viewBandwidth;
    }

    void WaterFall::setViewOffset(double offset) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        if (offset == viewOffset) {
            return;
        }
        if (offset - (viewBandwidth / 2.0) < -(wholeBandwidth / 2.0)) {
            offset = (viewBandwidth / 2.0) - (wholeBandwidth / 2.0);
        }
        if (offset + (viewBandwidth / 2.0) > (wholeBandwidth / 2.0)) {
            offset = (wholeBandwidth / 2.0) - (viewBandwidth / 2.0);
        }
        viewOffset = offset;
        lowerFreq = (centerFreq + viewOffset) - (viewBandwidth / 2.0);
        upperFreq = (centerFreq + viewOffset) + (viewBandwidth / 2.0);
        if (_fullUpdate) { waterfallUpdate = true; };
        updateAllVFOs();
    }

    double WaterFall::getViewOffset() {
        return viewOffset;
    }

    void WaterFall::setFFTMin(float min) {
        fftMin = min;
        verticalFftRange = findBestFrequencyRange(fftMax - fftMin, maxVerticalFftSteps);
    }

    float WaterFall::getFFTMin() {
        return fftMin;
    }

    void WaterFall::setFFTMax(float max) {
        fftMax = max;
        verticalFftRange = findBestFrequencyRange(fftMax - fftMin, maxVerticalFftSteps);
    }

    float WaterFall::getFFTMax() {
        return fftMax;
    }

    void WaterFall::setFullWaterfallUpdate(bool fullUpdate) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        _fullUpdate = fullUpdate;
    }

    void WaterFall::setWaterfallMin(float min) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        if (min == waterfallMin) {
            return;
        }
        waterfallMin = min;
        if (_fullUpdate) { waterfallUpdate = true; };
    }

    float WaterFall::getWaterfallMin() {
        return waterfallMin;
    }

    void WaterFall::setWaterfallMax(float max) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        if (max == waterfallMax) {
            return;
        }
        waterfallMax = max;
        if (_fullUpdate) { waterfallUpdate = true; };
    }

    float WaterFall::getWaterfallMax() {
        return waterfallMax;
    }

    void WaterFall::updateAllVFOs(bool checkRedrawRequired) {
        for (auto const& [name, vfo] : vfos) {
            if (checkRedrawRequired && !vfo->redrawRequired) { continue; }
            vfo->updateDrawingVars(viewBandwidth, dataWidth, viewOffset, widgetPos, fftHeight);
            vfo->wfRectMin = ImVec2(vfo->rectMin.x, wfAreaMin.y);
            vfo->wfRectMax = ImVec2(vfo->rectMax.x, wfAreaMax.y);
            vfo->wfLineMin = ImVec2(vfo->lineMin.x, wfAreaMin.y - 1);
            vfo->wfLineMax = ImVec2(vfo->lineMax.x, wfAreaMax.y - 1);
            vfo->wfLbwSelMin = ImVec2(vfo->wfRectMin.x - 2, vfo->wfRectMin.y);
            vfo->wfLbwSelMax = ImVec2(vfo->wfRectMin.x + 2, vfo->wfRectMax.y);
            vfo->wfRbwSelMin = ImVec2(vfo->wfRectMax.x - 2, vfo->wfRectMin.y);
            vfo->wfRbwSelMax = ImVec2(vfo->wfRectMax.x + 2, vfo->wfRectMax.y);
            vfo->redrawRequired = false;
        }
    }

    void WaterFall::setRawFFTSize(int size) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        rawFFTSize = size;

        if (inputBuffer != NULL) {
            inputBuffer = (float*)realloc(inputBuffer, rawFFTSize * sizeof(float));
        }
        else {
            inputBuffer = (float*)malloc(rawFFTSize * sizeof(float));
        }
        fftLines = 0;
        waterfallUpdate = true;
    }

    void WaterFall::setBandPlanPos(int pos) {
        bandPlanPos = pos;
    }

    void WaterFall::setFFTRate(int rate) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        fftRate = rate;
        onResize();
    }

    void WaterFall::setFFTHold(bool hold) {
        fftHold = hold;
        if (fftHold && latestFFTHold) {
            for (int i = 0; i < dataWidth; i++) {
                latestFFTHold[i] = -1000.0;
            }
        }
    }

    void WaterFall::setFFTHoldSpeed(float speed) {
        fftHoldSpeed = speed;
    }

    void WaterFall::setFFTSmoothing(bool enabled) {
        std::lock_guard<std::mutex> lck(smoothingBufMtx);
        fftSmoothing = enabled;

        // Free buffer if not null
        if (smoothingBuf) {delete[] smoothingBuf; }

        // If disabled, stop here
        if (!enabled) {
            smoothingBuf = NULL;
            return;
        }

        // Allocate and copy existing FFT into it
        smoothingBuf = new float[dataWidth];
        if (latestFFT) {
            std::lock_guard<std::recursive_mutex> lck2(latestFFTMtx);
            memcpy(smoothingBuf, latestFFT, dataWidth * sizeof(float));
        }
        else {
            memset(smoothingBuf, 0, dataWidth * sizeof(float));
        }
    }

    void WaterFall::setFFTSmoothingSpeed(float speed) {
        std::lock_guard<std::mutex> lck(smoothingBufMtx);
        fftSmoothingAlpha = speed;
        fftSmoothingBeta = 1.0f - speed;
    }

    void WaterFall::setSNRSmoothing(bool enabled) {
        snrSmoothing = enabled;
    }

    void WaterFall::setSNRSmoothingSpeed(float speed) {
        snrSmoothingAlpha = speed;
        snrSmoothingBeta = 1.0f - speed;
    }

    float* WaterFall::acquireLatestFFT(int& width) {
        latestFFTMtx.lock();
        if (!latestFFT) {
            latestFFTMtx.unlock();
            return NULL;
        }
        width = dataWidth;
        return latestFFT;
    }

    void WaterFall::releaseLatestFFT() {
        latestFFTMtx.unlock();
    }

    void WaterfallVFO::setOffset(double offset) {
        generalOffset = offset;
        if (reference == REF_CENTER) {
            centerOffset = offset;
            lowerOffset = offset - (bandwidth / 2.0);
            upperOffset = offset + (bandwidth / 2.0);
        }
        else if (reference == REF_LOWER) {
            lowerOffset = offset;
            centerOffset = offset + (bandwidth / 2.0);
            upperOffset = offset + bandwidth;
        }
        else if (reference == REF_UPPER) {
            upperOffset = offset;
            centerOffset = offset - (bandwidth / 2.0);
            lowerOffset = offset - bandwidth;
        }
        centerOffsetChanged = true;
        upperOffsetChanged = true;
        lowerOffsetChanged = true;
        redrawRequired = true;
    }

    void WaterfallVFO::setCenterOffset(double offset) {
        if (reference == REF_CENTER) {
            generalOffset = offset;
        }
        else if (reference == REF_LOWER) {
            generalOffset = offset - (bandwidth / 2.0);
        }
        else if (reference == REF_UPPER) {
            generalOffset = offset + (bandwidth / 2.0);
        }
        centerOffset = offset;
        lowerOffset = offset - (bandwidth / 2.0);
        upperOffset = offset + (bandwidth / 2.0);
        centerOffsetChanged = true;
        upperOffsetChanged = true;
        lowerOffsetChanged = true;
        redrawRequired = true;
    }

    void WaterfallVFO::setBandwidth(double bw) {
        if (bandwidth == bw || bw < 0) {
            return;
        }
        bandwidth = bw;
        if (reference == REF_CENTER) {
            lowerOffset = centerOffset - (bandwidth / 2.0);
            upperOffset = centerOffset + (bandwidth / 2.0);
        }
        else if (reference == REF_LOWER) {
            centerOffset = lowerOffset + (bandwidth / 2.0);
            upperOffset = lowerOffset + bandwidth;
            centerOffsetChanged = true;
        }
        else if (reference == REF_UPPER) {
            centerOffset = upperOffset - (bandwidth / 2.0);
            lowerOffset = upperOffset - bandwidth;
            centerOffsetChanged = true;
        }
        bandwidthChanged = true;
        redrawRequired = true;
    }

    void WaterfallVFO::setReference(int ref) {
        if (reference == ref || ref < 0 || ref >= _REF_COUNT) {
            return;
        }
        reference = ref;
        setOffset(generalOffset);
    }

    void WaterfallVFO::setNotchOffset(double offset) {
        notchOffset = offset;
        redrawRequired = true;
    }

    void WaterfallVFO::setNotchVisible(bool visible) {
        notchVisible = visible;
        redrawRequired = true;
    }

    void WaterfallVFO::updateDrawingVars(double viewBandwidth, float dataWidth, double viewOffset, ImVec2 widgetPos, int fftHeight) {
        int center = roundf((((centerOffset - viewOffset) / (viewBandwidth / 2.0)) + 1.0) * ((double)dataWidth / 2.0));
        int left = roundf((((lowerOffset - viewOffset) / (viewBandwidth / 2.0)) + 1.0) * ((double)dataWidth / 2.0));
        int right = roundf((((upperOffset - viewOffset) / (viewBandwidth / 2.0)) + 1.0) * ((double)dataWidth / 2.0));
        int notch = roundf((((notchOffset + centerOffset - viewOffset) / (viewBandwidth / 2.0)) + 1.0) * ((double)dataWidth / 2.0));

        // Check weather the line is visible
        if (left >= 0 && left < dataWidth && reference == REF_LOWER) {
            lineVisible = true;
        }
        else if (center >= 0 && center < dataWidth && reference == REF_CENTER) {
            lineVisible = true;
        }
        else if (right >= 0 && right < dataWidth && reference == REF_UPPER) {
            lineVisible = true;
        }
        else {
            lineVisible = false;
        }

        // Calculate the position of the line
        if (reference == REF_LOWER) {
            lineMin = ImVec2(gui::waterfall.fftAreaMin.x + left, gui::waterfall.fftAreaMin.y);
            lineMax = ImVec2(gui::waterfall.fftAreaMin.x + left, gui::waterfall.fftAreaMax.y - 1);
        }
        else if (reference == REF_CENTER) {
            lineMin = ImVec2(gui::waterfall.fftAreaMin.x + center, gui::waterfall.fftAreaMin.y);
            lineMax = ImVec2(gui::waterfall.fftAreaMin.x + center, gui::waterfall.fftAreaMax.y - 1);
        }
        else if (reference == REF_UPPER) {
            lineMin = ImVec2(gui::waterfall.fftAreaMin.x + right, gui::waterfall.fftAreaMin.y);
            lineMax = ImVec2(gui::waterfall.fftAreaMin.x + right, gui::waterfall.fftAreaMax.y - 1);
        }

        int _left = left;
        int _right = right;
        left = std::clamp<int>(left, 0, dataWidth - 1);
        right = std::clamp<int>(right, 0, dataWidth - 1);
        leftClamped = (left != _left);
        rightClamped = (right != _right);

        rectMin = ImVec2(gui::waterfall.fftAreaMin.x + left, gui::waterfall.fftAreaMin.y + 1);
        rectMax = ImVec2(gui::waterfall.fftAreaMin.x + right + 1, gui::waterfall.fftAreaMax.y);

        float gripSize = 2.0f * style::uiScale;
        lbwSelMin = ImVec2(rectMin.x - gripSize, rectMin.y);
        lbwSelMax = ImVec2(rectMin.x + gripSize, rectMax.y);
        rbwSelMin = ImVec2(rectMax.x - gripSize, rectMin.y);
        rbwSelMax = ImVec2(rectMax.x + gripSize, rectMax.y);

        notchMin = ImVec2(gui::waterfall.fftAreaMin.x + notch - gripSize, gui::waterfall.fftAreaMin.y);
        notchMax = ImVec2(gui::waterfall.fftAreaMin.x + notch + gripSize, gui::waterfall.fftAreaMax.y - 1);
    }

    void WaterfallVFO::draw(ImGuiWindow* window, bool selected) {
        window->DrawList->AddRectFilled(rectMin, rectMax, color);
        if (lineVisible) {
            window->DrawList->AddLine(lineMin, lineMax, selected ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 255, 0, 255), style::uiScale);
        }

        if (notchVisible) {
            window->DrawList->AddRectFilled(notchMin, notchMax, IM_COL32(255, 0, 0, 127));
        }

        if (!gui::mainWindow.processMouseInputs && !gui::waterfall.inputHandled) {
            ImVec2 mousePos = ImGui::GetMousePos();
            if (rectMax.x - rectMin.x < 10) { return; }
            if (reference != REF_LOWER && !bandwidthLocked && !leftClamped) {
                if (IS_IN_AREA(mousePos, lbwSelMin, lbwSelMax)) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); }
                else if (IS_IN_AREA(mousePos, wfLbwSelMin, wfLbwSelMax)) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }
            }
            if (reference != REF_UPPER && !bandwidthLocked && !rightClamped) {
                if (IS_IN_AREA(mousePos, rbwSelMin, rbwSelMax)) { ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); }
                else if (IS_IN_AREA(mousePos, wfRbwSelMin, wfRbwSelMax)) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }
            }
        }
    }

    void WaterFall::showWaterfall() {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        if (inputBuffer == NULL) {
            flog::error("Null inputBuffer");
            return;
        }
        waterfallVisible = true;
        onResize();
        if (rawFFTs) {
            memset(rawFFTs, 0, waterfallHeight * dataWidth * sizeof(uint8_t));
        }
        waterfallUpdate = true;
    }

    void WaterFall::hideWaterfall() {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        waterfallVisible = false;
        onResize();
    }

    void WaterFall::setFFTHeight(int height) {
        std::lock_guard<std::recursive_mutex> lck(buf_mtx);
        FFTAreaHeight = height;
        newFFTAreaHeight = height;
        onResize();
    }

    int WaterFall::getFFTHeight() {
        return FFTAreaHeight;
    }

    void WaterFall::showBandplan() {
        bandplanVisible = true;
    }

    void WaterFall::hideBandplan() {
        bandplanVisible = false;
    }

    void WaterfallVFO::setSnapInterval(double interval) {
        snapInterval = interval;
    }
};