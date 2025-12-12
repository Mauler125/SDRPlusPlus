#pragma once
#include "cosine.h"

namespace dsp::window {
    inline double flatTop(const double n, const double N, const void* const /*params*/) {
        const double coefs[] = { 1.0, 1.93, 1.29, 0.388, 0.028 };
        return cosine(n, N, coefs, sizeof(coefs) / sizeof(double));
    }
}
