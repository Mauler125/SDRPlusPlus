#pragma once
#include <math.h>
#include "rectangular.h"
#include "hann.h"
#include "../math/constants.h"

namespace dsp::window {
    struct TukeyParams {
        double alpha = 0.5;
    };

    inline double tukey(const double n, const double N, const void* const params) {
        const TukeyParams* tp = (TukeyParams*)params;
        const double alpha = tp->alpha;

        if (alpha <= 0.0) {
            return rectangular(n, N, params);
        }
        if (alpha >= 1.0) {
            // Hann window
            return 0.5 * (1.0 - cos(2.0 * DB_M_PI * n / N));
        }

        const double edge = alpha * N / 2.0;

        if (n < edge) {
            // Rising cosine taper
            return 0.5 * (1.0 + cos(DB_M_PI * ((2.0 * n) / (alpha * N) - 1.0)));
        }

        if (n <= N * (1.0 - alpha/2.0)) {
            return rectangular(n, N, params);
        }

        // Falling cosine taper
        return 0.5 * (1.0 + cos(DB_M_PI * ((2.0 * n) / (alpha * N) - 2.0 / alpha + 1.0)));
    }
}
