#pragma once
#include "block.h"

namespace dsp {
    template <class T>
    class Sink : public block {
    public:
        Sink() {}

        Sink(stream<T>* in) { init(in); }

        virtual ~Sink() {
            if (!_block_init) { return; }
            shutdown();
        }

        virtual void init(stream<T>* in) {
            _in = in;
            registerInput(_in);
            _block_init = true;
        }

        virtual void shutdown() {
            _block_init = false;
            stop();
            unregisterInput(_in);
            _in = nullptr;
        }

        virtual void setInput(stream<T>* in) {
            assert(_block_init);
            std::lock_guard<std::recursive_mutex> lck(ctrlMtx);
            tempStop();
            unregisterInput(_in);
            _in = in;
            registerInput(_in);
            tempStart();
        }

        virtual int run() = 0;

    protected:
        stream<T>* _in;
    };
}
