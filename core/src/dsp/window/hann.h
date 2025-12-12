#pragma once
#include "../math/constants.h"
#include <cmath>

namespace dsp::window {
    struct HannParams {
        double alpha = 0.5;
    };

    inline double hann(const double n, const double N, const void* const params) {
        const HannParams* hp = (HannParams*)params;

        const double scale = N;
        const double alpha = hp->alpha;

        return alpha - (1.0 - alpha) * cos(2.0 * DB_M_PI * n / scale);
    }
}
