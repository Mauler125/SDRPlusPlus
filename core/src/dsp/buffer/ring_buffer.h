#pragma once
#include "buffer.h"
#include "dsp/math/bitop.h"
#include <atomic>

#define RING_BUF_SZ 0x100000

namespace dsp::buffer {
    template <class T>
    class RingBuffer {
    public:
        RingBuffer() {}
        RingBuffer(int maxLatency, int bufferSize = RING_BUF_SZ) { init(maxLatency, bufferSize); }

        ~RingBuffer() {
            if (memory) {
                buffer::free(memory);
            }
        }

        void init(int maxLatency, int bufferSize = RING_BUF_SZ) {
            size = bufferSize;
            isPow2 = math::isPow2(size);
            mask = isPow2 ? (size - 1) : 0;
            _stopReader = false;
            _stopWriter = false;
            this->maxLatency = maxLatency;
            readCursor = 0;
            writeCursor = 0;
            readable = 0;
            writable = size;
            if (memory) { buffer::free(memory); }
            memory = buffer::alloc<T>(size);
            buffer::clear(memory, size);
        }

        void shutdown() {
            if (memory) {
                buffer::free(memory);
                memory = NULL;
            }
        }

        int read(T* data, int len) {
            return readInternal(data, len);
        }

        int readAndSkip(T* data, int len, int skip) {
            int readCount = readInternal(data, len);
            if (readCount < 0) { return -1; }
            return readInternal(nullptr, skip);
        }

        int write(T* data, int len) {
            return writeInternal(data, len);
        }

        void stopReader() {
            _stopReader = true;
            canReadVar.notify_one();
        }
        void stopWriter() {
            _stopWriter = true;
            canWriteVar.notify_one();
        }
        bool getReadStop() { return _stopReader; }
        bool getWriteStop() { return _stopWriter; }
        void clearReadStop() { _stopReader = false; }
        void clearWriteStop() { _stopWriter = false; }
        void setMaxLatency(int maxLatency) { this->maxLatency = maxLatency; }

    private:
        int waitReadable() {
            std::unique_lock<std::mutex> lck(_readableMtx);
            canReadVar.wait(lck, [this]() { return readable > 0 || _stopReader; });
            return _stopReader ? -1 : readable;
        }

        int waitWritable() {
            std::unique_lock<std::mutex> lck(_writableMtx);
            canWriteVar.wait(lck, [this]() { return writable > 0 || _stopWriter; });
            return _stopWriter ? -1 : writable;
        }

        inline int getBufferIndex(int pos) {
            return isPow2 ? (pos & mask) : (pos % size);
        }

        int readInternal(T* data, int len) {
            int processed = 0;
            while (processed < len) {
                int toRead = waitReadable();
                if (toRead < 0) { return -1; }
                toRead = std::min<int>(toRead, len - processed);

                copyFromBuffer(data ? &data[processed] : nullptr, readCursor, toRead);
                readCursor = getBufferIndex(readCursor + toRead);

                {
                    std::lock_guard<std::mutex> lck(_readableMtx);
                    readable -= toRead;
                }
                {
                    std::lock_guard<std::mutex> lck(_writableMtx);
                    writable += toRead;
                }

                canWriteVar.notify_one();
                processed += toRead;
            }
            return len;
        }

        int writeInternal(T* data, int len) {
            int processed = 0;
            while (processed < len) {
                int toWrite = waitWritable();
                if (toWrite < 0) { return -1; }
                toWrite = std::min<int>(toWrite, len - processed);

                copyToBuffer(data ? &data[processed] : nullptr, writeCursor, toWrite);
                writeCursor = getBufferIndex(writeCursor + toWrite);

                {
                    std::lock_guard<std::mutex> lck(_readableMtx);
                    readable += toWrite;
                }
                {
                    std::lock_guard<std::mutex> lck(_writableMtx);
                    writable -= toWrite;
                }

                canReadVar.notify_one();
                processed += toWrite;
            }
            return len;
        }

        void copyFromBuffer(T* dest, int cursor, int len) {
            assert(memory);
            if (!dest) { return; }
            int firstChunk = std::min<int>(len, size - cursor);
            memcpy(dest, &memory[cursor], firstChunk * sizeof(T));
            if (firstChunk < len) {
                memcpy(dest + firstChunk, &memory[0], (len - firstChunk) * sizeof(T));
            }
        }

        void copyToBuffer(T* src, int cursor, int len) {
            assert(memory);
            if (!src) { return; }
            int firstChunk = std::min<int>(len, size - cursor);
            memcpy(&memory[cursor], src, firstChunk * sizeof(T));
            if (firstChunk < len) {
                memcpy(&memory[0], src + firstChunk, (len - firstChunk) * sizeof(T));
            }
        }

        T* memory = nullptr;
        std::mutex _readableMtx;
        std::mutex _writableMtx;
        std::condition_variable canReadVar;
        std::condition_variable canWriteVar;
        int size;
        int mask;
        int readCursor;
        int writeCursor;
        int readable;
        int writable;
        int maxLatency;
        std::atomic_bool _stopReader;
        std::atomic_bool _stopWriter;
        bool isPow2;
    };
}