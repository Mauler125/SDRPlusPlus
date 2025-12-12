#pragma once

namespace dsp::window {
    inline double rectangular(const double n, const double N, const void* const /*params*/) {
        return 1.0;
    }
}