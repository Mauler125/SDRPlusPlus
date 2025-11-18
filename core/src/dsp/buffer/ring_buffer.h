#pragma once
#include "buffer.h"
#include "dsp/math/bitop.h"

#if defined(__cpp_lib_hardware_interference_size) && !defined(__APPLE__)
static constexpr size_t hardwareInterferenceSize =
    std::hardware_destructive_interference_size;
#else
static constexpr size_t hardwareInterferenceSize = 64;
#endif

#define RING_BUFFER_MIN_SIZE 0x2        // Technically we can do 1, but then this class shouldn't be used.
#define RING_BUFFER_MAX_SIZE 0x40000000 // Anything higher will make (write - read) ambiguous after cursor wraparound!
#define RING_BUFFER_DEF_SIZE 0x100000

namespace dsp::buffer {

    template <class T>
    class RingBuffer {
    public:
        explicit RingBuffer(const int bufSize = RING_BUFFER_DEF_SIZE) {
            init(bufSize);
        }
        ~RingBuffer() = default;

        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;
        RingBuffer(RingBuffer&&) = delete;
        RingBuffer& operator=(RingBuffer&&) = delete;

        void init(const int bufSize = RING_BUFFER_DEF_SIZE) {
            assert(bufSize >= RING_BUFFER_MIN_SIZE);
            assert(bufSize <= RING_BUFFER_MAX_SIZE);

            // This is so we can use bitwise AND to calc our indices,
            // because modulo is about the slowest opcode to exist.
            capacity = math::nextPow2(static_cast<uint32_t>(bufSize));
            mask = capacity - 1;

            memory = std::make_unique<T[]>(capacity);
        }

        int write(const T* const data, const int count) {
            if (count <= 0) {
                return 0;
            }

            const uint32_t currWrite = writeCursor.load(std::memory_order_relaxed);
            const uint32_t currRead = readCursor.load(std::memory_order_acquire);

            const uint32_t writable = capacity - (currWrite - currRead);
            const uint32_t toWrite = std::min<uint32_t>(static_cast<uint32_t>(count), writable);

            if (toWrite == 0) {
                return 0;
            }

            copyToBuffer(data, currWrite & mask, toWrite);
            writeCursor.store(currWrite + toWrite, std::memory_order_release);

            return static_cast<int>(toWrite);
        }

        int read(T* const data, const int count) {
            if (count <= 0) {
                return 0;
            }

            const uint32_t currRead = readCursor.load(std::memory_order_relaxed);
            const uint32_t currWrite = writeCursor.load(std::memory_order_acquire);

            const uint32_t readable = currWrite - currRead;
            const uint32_t toRead = std::min<uint32_t>(static_cast<uint32_t>(count), readable);

            if (toRead == 0) {
                return 0;
            }
            
            copyFromBuffer(data, currRead & mask, toRead);
            readCursor.store(currRead + toRead, std::memory_order_release);

            return static_cast<int>(toRead);
        }

        int readAndSkip(T* const data, const int len, const int skip) {
            const int totalToConsume = len + skip;
            if (totalToConsume <= 0) {
                return 0;
            }

            const uint32_t currRead = readCursor.load(std::memory_order_relaxed);
            const uint32_t currWrite = writeCursor.load(std::memory_order_acquire);

            const uint32_t readable = currWrite - currRead;
            const uint32_t actualToConsume = std::min<uint32_t>(totalToConsume, readable);

            if (actualToConsume == 0) {
                return 0;
            }

            const uint32_t readRequest = (len > 0) ? static_cast<uint32_t>(len) : 0;
            const uint32_t actualToRead = std::min<uint32_t>(readRequest, actualToConsume);

            if (actualToRead > 0) {
                copyFromBuffer(data, currRead & mask, actualToRead);
            }

            readCursor.store(currRead + actualToConsume, std::memory_order_release);
            return static_cast<int>(actualToRead);
        }

        int getReadableSize() const {
            const uint32_t readable = writeCursor.load(std::memory_order_acquire) - readCursor.load(std::memory_order_relaxed);
            return static_cast<int>(std::min<uint32_t>(readable, static_cast<uint32_t>((std::numeric_limits<int>::max)())));
        }

        int getWritableSize() const {
            return static_cast<int>(capacity) - getReadableSize();
        }

        int getCapacity() const {
            return static_cast<int>(capacity);
        }

    private:
        void copyToBuffer(const T* const data, const uint32_t idx, const uint32_t count) {
            if (idx + count <= capacity) {
                memcpy(&memory[idx], data, count * sizeof(T));
            } else {
                const uint32_t firstChunkSz = capacity - idx;
                memcpy(&memory[idx], data, firstChunkSz * sizeof(T));
                memcpy(&memory[0], data + firstChunkSz, (count - firstChunkSz) * sizeof(T));
            }
        }
        
        void copyFromBuffer(T* const data, const uint32_t idx, const uint32_t count) {
            if (idx + count <= capacity) {
                memcpy(data, &memory[idx], count * sizeof(T));
            } else {
                const uint32_t firstChunkSz = capacity - idx;
                memcpy(data, &memory[idx], firstChunkSz * sizeof(T));
                memcpy(data + firstChunkSz, &memory[0], (count - firstChunkSz) * sizeof(T));
            }
        }
        
        alignas(hardwareInterferenceSize) std::atomic<uint32_t> writeCursor{ 0 };
        alignas(hardwareInterferenceSize) std::atomic<uint32_t> readCursor{ 0 };

        uint32_t capacity = 0;
        uint32_t mask = 0;
        std::unique_ptr<T[]> memory;
    };
}
