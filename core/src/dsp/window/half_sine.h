#pragma once
#include "../math/constants.h"
#include <cmath>

namespace dsp::window {
    inline double halfSine(const double n, const double N, const void* const /*params*/) {
        return sin(DB_M_PI * n / N);
    }
}
