#pragma once

namespace dsp::math {
    template <typename T>
    inline bool isPow2(T x) {
        return (x & (x - 1)) == 0;
    }

    template <typename T>
    inline T nextPow2(T n) {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        if constexpr (sizeof(T) >= 2) { n |= n >> 8; }
        if constexpr (sizeof(T) >= 4) { n |= n >> 16; }
        if constexpr (sizeof(T) >= 8) { n |= n >> 32; }
        n++;
        return n;
    }
}