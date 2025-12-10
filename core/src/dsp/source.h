#pragma once
#include "block.h"

namespace dsp {
    template <class T>
    class Source : public block {
    public:
        Source() { init(); }

        virtual ~Source() { shutdown(); }

        virtual void init() {
            registerOutput(&out);
            _block_init = true;
        }

        virtual void shutdown() {
            _block_init = false;
            unregisterOutput(&out);
        }

        virtual int run() = 0;

        stream<T> out;
    };
}
