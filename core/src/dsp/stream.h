#pragma once
#include <string.h>
#include <mutex>
#include <condition_variable>
#include <volk/volk.h>
#include "buffer/buffer.h"

// 1MSample buffer
#define STREAM_BUFFER_SIZE 1000000

namespace dsp {
    class untyped_stream {
    public:
        virtual ~untyped_stream() {}
        virtual bool swap(int size) { return false; }
        virtual int read() { return -1; }
        virtual void flush() {}
        virtual void stopWriter() {}
        virtual void clearWriteStop() {}
        virtual void stopReader() {}
        virtual void clearReadStop() {}
    };

    template <class T>
    class stream : public untyped_stream {
    public:
        stream() {
            writeBuf = buffer::alloc<T>(STREAM_BUFFER_SIZE);
            readBuf = buffer::alloc<T>(STREAM_BUFFER_SIZE);
        }

        virtual ~stream() {
            free();
        }

        virtual void setBufferSize(int samples) {
            std::lock_guard<std::mutex> lck(bufMtx);
            buffer::free(writeBuf);
            buffer::free(readBuf);
            writeBuf = buffer::alloc<T>(samples);
            readBuf = buffer::alloc<T>(samples);
        }

        virtual inline bool swap(int size) {
            {
                // Wait to either swap or stop
                std::unique_lock<std::mutex> lck(swapMtx);
                swapCV.wait(lck, [this] { return (canSwap || writerStop); });

                // If writer was stopped, abandon operation
                if (writerStop) { return false; }

                // Swap buffers
                dataSize = size;
                T* temp = writeBuf;
                writeBuf = readBuf;
                readBuf = temp;
                canSwap = false;
            }

            // Notify reader that some data is ready
            {
                std::lock_guard<std::mutex> lck(rdyMtx);
                dataReady = true;
            }
            rdyCV.notify_all();

            return true;
        }

        virtual inline int read() {
            // Wait for data to be ready or to be stopped
            std::unique_lock<std::mutex> lck(rdyMtx);
            rdyCV.wait(lck, [this] { return (dataReady || readerStop); });

            if (readerStop) {
                return -1;
            }

            bufMtx.lock();
            return dataSize;
        }

        virtual inline void flush() {
            // Clear data ready
            {
                std::lock_guard<std::mutex> lck(rdyMtx);
                dataReady = false;
            }

            bufMtx.unlock();

            // Notify writer that buffers can be swapped
            {
                std::lock_guard<std::mutex> lck(swapMtx);
                canSwap = true;
            }

            swapCV.notify_all();
        }

        virtual void stopWriter() {
            {
                std::lock_guard<std::mutex> lck(swapMtx);
                writerStop = true;
            }
            swapCV.notify_all();
        }

        virtual void clearWriteStop() {
            writerStop = false;
        }

        virtual void stopReader() {
            {
                std::lock_guard<std::mutex> lck(rdyMtx);
                readerStop = true;
            }
            rdyCV.notify_all();
        }

        virtual void clearReadStop() {
            readerStop = false;
        }

        void free() {
            std::lock_guard<std::mutex> lck(bufMtx);
            if (writeBuf) {
                buffer::free(writeBuf);
                writeBuf = NULL;
            }
            if (readBuf) {
                buffer::free(readBuf);
                readBuf = NULL;
            }
        }

        T* writeBuf;
        T* readBuf;

    private:
        std::mutex swapMtx;
        std::condition_variable swapCV;

        std::mutex rdyMtx;
        std::condition_variable rdyCV;

        std::mutex bufMtx;

        bool canSwap = true;
        bool dataReady = false;

        bool readerStop = false;
        bool writerStop = false;

        int dataSize = 0;
    };
}