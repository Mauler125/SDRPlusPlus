#pragma once
#include "../math/constants.h"
#include <cmath>

namespace dsp::window {
    inline double bartlettHann(const double n, const double N, const void* const /*params*/) {
        const double a = n / N;
        return 0.62 - 0.48 * fabs(a - 0.5) - 0.38 * cos(2.0 * DB_M_PI * a);
    }
}
