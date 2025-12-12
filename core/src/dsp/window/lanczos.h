#pragma once
#include <cmath>

namespace dsp::window {
    inline double lanczos(const double n, const double N, const void* const /*params*/) {
        const double x = 2.0 * (n - N / 2.0) / N; // Scale to [-1,1]

        if (fabs(x) < 1e-12) { return 1.0; } // When n == N/2
        return sin(DB_M_PI * x) / (DB_M_PI * x);
    }
}
