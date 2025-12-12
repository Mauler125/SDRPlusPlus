#pragma once
#include "bessel.h"
#include <math.h>

namespace dsp::window {
    struct KaiserParams {
        double beta = 8.6;
    };

    inline double kaiser(const double n, const double N, const void* const params) {
        const double alpha = N / 2.0;
        const double t = (n - alpha) / alpha;

        const KaiserParams* kp = (KaiserParams*)params;

        const double beta = kp->beta;
        const double radicand = 1.0 - t * t;

        const double numer = besselI0(beta * sqrt(radicand));
        const double denom = besselI0(beta);

        return numer / denom;
    }
}
