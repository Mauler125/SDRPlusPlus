#pragma once
#include "cosine.h"

namespace dsp::window {
    inline double blackman(const double n, const double N, const void* const /*params*/) {
        const double coefs[] = { 0.42, 0.5, 0.08 };
        return cosine(n, N, coefs, sizeof(coefs) / sizeof(double));
    }
}