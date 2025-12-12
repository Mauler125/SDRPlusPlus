#pragma once
#include <cmath>

namespace dsp::window {
    struct GaussianParams {
        double sigma = 0.4; // Standard deviation relative to window length
    };

    inline double gaussian(const double n, const double N, const void* const params) {
        const GaussianParams* gp = (GaussianParams*)params;
        const double sigma = gp->sigma;
        const double alpha = N / 2.0;
        const double t = (n - alpha) / (sigma * alpha);
        return exp(-0.5 * t * t);
    }
}
