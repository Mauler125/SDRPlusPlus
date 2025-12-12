#pragma once
#include <cmath>

namespace dsp::window {
    inline double bartlett(const double n, const double N, const void* const /*params*/) {
        return 1.0 - fabs((n - N / 2.0) / (N / 2.0));
    }
}
