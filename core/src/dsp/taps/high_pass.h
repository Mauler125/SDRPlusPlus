#pragma once
#include "windowed_sinc.h"
#include "estimate_tap_count.h"
#include "../window/nuttall.h"

namespace dsp::taps {
    inline tap<float> highPass(double cutoff, double transWidth, double sampleRate, bool oddTapCount = false) {
        int count = estimateTapCount(transWidth, sampleRate);
        if (oddTapCount && !(count % 2)) { count++; }
        return windowedSinc<float>(count, (sampleRate / 2.0) - cutoff, sampleRate, [=](double n, double N, void* unused){
            return window::nuttall(n, N, unused) * (((int)round(n) % 2) ? -1.0f : 1.0f);
        });
    }
}