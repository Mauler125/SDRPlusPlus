#pragma once
#include <string.h>
#include <mutex>
#include <condition_variable>
#include <atomic>
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
        virtual void stopReader() {}
        virtual void clearReadStop() {}
        virtual void stopWriter() {}
        virtual void clearWriteStop() {}
    };

    template <class T>
    class stream : public untyped_stream {
    public:
        stream(bool doDefaultAlloc = true) {
            if (doDefaultAlloc) {
                alloc(STREAM_BUFFER_SIZE);
            }
        }

        virtual ~stream() {
            free();
        }

        virtual void setBufferSize(int samples) {
            if (readBuf) { buffer::free(readBuf); }
            readBuf = buffer::alloc<T>(samples);
            if (writeBuf) { buffer::free(writeBuf); }
            writeBuf = buffer::alloc<T>(samples);
            bufferSize = samples;
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

            return (readerStop ? -1 : dataSize);
        }

        virtual inline void flush() {
            // Clear data ready
            {
                std::lock_guard<std::mutex> lck(rdyMtx);
                dataReady = false;
            }

            // Notify writer that buffers can be swapped
            {
                std::lock_guard<std::mutex> lck(swapMtx);
                canSwap = true;
            }

            swapCV.notify_all();
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

        void alloc(int size) {
            readBuf = buffer::alloc<T>(size);
            writeBuf = buffer::alloc<T>(size);
            bufferSize = size;
        }

        void free() {
            if (readBuf) {
                buffer::free(readBuf);
                readBuf = NULL;
            }
            if (writeBuf) {
                buffer::free(writeBuf);
                writeBuf = NULL;
            }
            bufferSize = 0;
        }

        T* readBuf = nullptr;
        T* writeBuf = nullptr;

    private:
        std::mutex swapMtx;
        std::condition_variable swapCV;

        std::mutex rdyMtx;
        std::condition_variable rdyCV;

        bool canSwap = true;
        bool dataReady = false;

        std::atomic_bool readerStop = false;
        std::atomic_bool writerStop = false;

        int bufferSize = 0;
        int dataSize = 0;
    };
}