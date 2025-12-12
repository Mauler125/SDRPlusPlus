#pragma once
#include "cosine.h"

namespace dsp::window {
    inline double blackmanNuttall(const double n, const double N, const void* const /*params*/) {
        const double coefs[] = { 0.3635819, 0.4891775, 0.1365995, 0.0106411 };
        return cosine(n, N, coefs, sizeof(coefs) / sizeof(double));
    }
}