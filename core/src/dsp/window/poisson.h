#pragma once
#include <cmath>

namespace dsp::window {
    struct PoissonParams {
        double alpha = 5.0;
    };

    inline double poisson(const double n, const double N, const void* const params) {
        const PoissonParams* pp = (PoissonParams*)params;
        return exp(-pp->alpha * fabs(n - N / 2.0) / (N / 2.0));
    }
}
