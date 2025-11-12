#include "flog.h"
#include <cassert>
#include <mutex>
#include <chrono>
#include <string.h>
#include <inttypes.h>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#ifndef FLOG_ANDROID_TAG
#define FLOG_ANDROID_TAG    "flog"
#endif
#endif

#include "base_types.h"

#define FORMAT_BUF_SIZE 16
#define ESCAPE_CHAR     '\\'

namespace flog {
    std::mutex outMtx;

    const char* TYPE_STR[_TYPE_COUNT] = {
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR"
    };

#ifdef _WIN32
#define COLOR_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
    const WORD TYPE_COLORS[_TYPE_COUNT] = {
        FOREGROUND_GREEN | FOREGROUND_BLUE,
        FOREGROUND_GREEN,
        FOREGROUND_RED | FOREGROUND_GREEN,
        FOREGROUND_RED
    };
#else
#define COLOR_WHITE "\x1B[0m"
    const char* TYPE_COLORS[_TYPE_COUNT] = {
        "\x1B[36m",
        "\x1B[32m",
        "\x1B[33m",
        "\x1B[31m",
    };
#endif

#ifdef __ANDROID__
    const android_LogPriority TYPE_PRIORITIES[_TYPE_COUNT] = {
        ANDROID_LOG_DEBUG,
        ANDROID_LOG_INFO,
        ANDROID_LOG_WARN,
        ANDROID_LOG_ERROR
    };
#endif

    void __log__(Type type, const char* fmt, const std::vector<std::string>& args) {
        // Reserve a buffer for the final output
        int argCount = args.size();
        int fmtLen = strlen(fmt) + 1;
        int totSize = fmtLen;
        for (const auto& a : args) { totSize += a.size(); }
        std::string out;
        out.reserve(totSize);
        
        // Get output stream depending on type
        FILE* outStream = (type == TYPE_ERROR) ? stderr : stdout;

        // Parse format string
        bool escaped = false;
        int formatCounter = 0;
        bool inFormat = false;
        int formatLen = 0;
        char formatBuf[FORMAT_BUF_SIZE+1];
        for (int i = 0; i < fmtLen; i++) {
            // Get char
            const char c = fmt[i];

            // If this character is escaped, don't try to parse it
            if (escaped) {
                escaped = false;
                out += c;
                continue;
            }

            // State machine
            if (!inFormat && c != '{') {
                // Write to formatted output if not escape character
                if (c == ESCAPE_CHAR) {
                    escaped = true;
                }
                else {
                    out += c;
                }
            }
            else if (!inFormat) {
                // Start format mode
                inFormat = true;
            }
            else if (c == '}') {
                // Stop format mode
                inFormat = false;

                // Insert string value or error
                if (!formatLen) {
                    // Use format counter as ID if available or print wrong format string
                    if (formatCounter < argCount) {
                        out += args[formatCounter++];
                    }
                    else {
                        out += "{}";
                    }
                }
                else {
                    // Parse number
                    formatBuf[formatLen] = 0;
                    formatCounter = std::atoi(formatBuf);

                    // Use ID if available or print wrong format string
                    if (formatCounter < argCount) {
                        out += args[formatCounter];
                    }
                    else {
                        out += '{';
                        out += formatBuf;
                        out += '}';
                    }

                    // Increment format counter
                    formatCounter++;
                }

                // Reset format counter
                formatLen = 0;
            }
            else {
                // Add to format buffer 
                if (formatLen < FORMAT_BUF_SIZE) { formatBuf[formatLen++] = c; }
            }
        }

        // Get time
        auto now = std::chrono::system_clock::now();
        auto nowt = std::chrono::system_clock::to_time_t(now);

        // We used to use std::localtime, but this isn't thread safe.
        // This should cover all platforms we support (hopefully!).
        std::tm nowc;
#ifdef _WIN32
        localtime_s(&nowc, &nowt);
#else
        localtime_r(&nowt, &nowc);
#endif
        // Write to output
        {
            std::lock_guard<std::mutex> lck(outMtx);
#if defined(_WIN32)
            // Get output handle and return if invalid
            int wOutStream = (type == TYPE_ERROR) ? STD_ERROR_HANDLE  : STD_OUTPUT_HANDLE;
            HANDLE conHndl = GetStdHandle(wOutStream);
            if (!conHndl || conHndl == INVALID_HANDLE_VALUE) { return; }

            // Print beginning of log line
            SetConsoleTextAttribute(conHndl, COLOR_WHITE);
            fprintf(outStream, "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [", nowc.tm_mday, nowc.tm_mon + 1, nowc.tm_year + 1900, nowc.tm_hour, nowc.tm_min, nowc.tm_sec, 0);

            // Switch color to the log color, print log type and 
            SetConsoleTextAttribute(conHndl, TYPE_COLORS[type]);
            fputs(TYPE_STR[type], outStream);
            

            // Switch back to default color and print rest of log string
            SetConsoleTextAttribute(conHndl, COLOR_WHITE);
            fprintf(outStream, "] %s\n", out.c_str());
#elif defined(__ANDROID__)
            // Print format string
            __android_log_print(TYPE_PRIORITIES[type], FLOG_ANDROID_TAG, COLOR_WHITE "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [%s%s" COLOR_WHITE "] %s\n",
                    nowc.tm_mday, nowc.tm_mon + 1, nowc.tm_year + 1900, nowc.tm_hour, nowc.tm_min, nowc.tm_sec, 0, TYPE_COLORS[type], TYPE_STR[type], out.c_str());
#else
            // Print format string
            fprintf(outStream, COLOR_WHITE "[%02d/%02d/%02d %02d:%02d:%02d.%03d] [%s%s" COLOR_WHITE "] %s\n",
                    nowc.tm_mday, nowc.tm_mon + 1, nowc.tm_year + 1900, nowc.tm_hour, nowc.tm_min, nowc.tm_sec, 0, TYPE_COLORS[type], TYPE_STR[type], out.c_str());
#endif
        }
    }

    template <typename T, int N>
    static std::string Flog_toString(const T value, const char* const pri) {
        char buf[N];
        const int ret = snprintf(buf, N, pri, value);
        assert(ret > 0); // Ensure snprintf succeeded.
        return { buf, (size_t)std::clamp<int>(ret, 0, N) };
    }

    std::string __toString__(bool value) {
        return value ? "true" : "false";
    }

    std::string __toString__(char value) {
        return std::string(1, value);
    }

    std::string __toString__(int8_t value) {
        return Flog_toString<int8_t, I8_STR_BUF_SIZE>(value, "%" PRId8);
    }

    std::string __toString__(int16_t value) {
        return Flog_toString<int16_t, I16_STR_BUF_SIZE>(value, "%" PRId16);
    }

    std::string __toString__(int32_t value) {
        return Flog_toString<int32_t, I32_STR_BUF_SIZE>(value, "%" PRId32);
    }

    std::string __toString__(int64_t value) {
        return Flog_toString<int64_t, I64_STR_BUF_SIZE>(value, "%" PRId64);
    }

    std::string __toString__(uint8_t value) {
        return Flog_toString<uint8_t, I8_STR_BUF_SIZE>(value, "%" PRIu8);
    }

    std::string __toString__(uint16_t value) {
        return Flog_toString<uint16_t, I16_STR_BUF_SIZE>(value, "%" PRIu16);
    }

    std::string __toString__(uint32_t value) {
        return Flog_toString<uint32_t, I32_STR_BUF_SIZE>(value, "%" PRIu32);
    }

    std::string __toString__(uint64_t value) {
        return Flog_toString<uint64_t, I64_STR_BUF_SIZE>(value, "%" PRIu64);
    }

    std::string __toString__(float value) {
        return Flog_toString<float, FLT_STR_BUF_SIZE>(value, "%g");
    }

    std::string __toString__(double value) {
        return Flog_toString<double, DBL_STR_BUF_SIZE>(value, "%lg");
    }

    std::string __toString__(const char* value) {
        return value;
    }

    std::string __toString__(const void* value) {
        // +2 for 0x prefix.
        return Flog_toString<const void*, PTR_STR_BUF_SIZE + 2>(value, "0x%p");
    }
}