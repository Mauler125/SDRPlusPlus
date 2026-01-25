#include "str_tools.h"
#include <regex>

static const std::regex s_validationPattern(R"((?:[A-Za-z0-9+\/]{4}?)*(?:[A-Za-z0-9+\/]{2}==|[A-Za-z0-9+\/]{3}=))");
static constexpr char s_encodeLookupTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static constexpr unsigned char s_decodeLookupTable[256] = {
    // 0-127
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 62, 0xFF, 0xFF, 0xFF, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 128-255
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

namespace utils {
    bool isValidBase64(const std::string& input, std::string* const output) {
        if (input.size() % 4 != 0) { return false; }

        std::smatch mh;

        if (std::regex_match(input, mh, s_validationPattern)) {
            if (output) {
                *output = mh[0].str();
            }

            return true;
        }

        return false;
    }

    bool encodeBase64(const std::string_view& input, std::string& output) {
        if (input.empty()) { return false; }

        const size_t inLen = input.size();
        const size_t outLen = 4 * ((inLen + 2) / 3);

        output.resize(outLen);

        size_t i = 0;
        size_t j = 0;

        const unsigned char* data = (const unsigned char*)input.data();
        const size_t limit = inLen - (inLen % 3);

        for (i = 0; i < limit; i += 3) {
            const uint32_t val = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];

            output[j++] = s_encodeLookupTable[(val >> 18) & 0x3F];
            output[j++] = s_encodeLookupTable[(val >> 12) & 0x3F];
            output[j++] = s_encodeLookupTable[(val >> 6) & 0x3F];
            output[j++] = s_encodeLookupTable[val & 0x3F];
        }

        // Handle remainder
        if (i < inLen) {
            uint32_t val = data[i] << 16;
            if (i + 1 < inLen) { val |= data[i + 1] << 8; }

            output[j++] = s_encodeLookupTable[(val >> 18) & 0x3F];
            output[j++] = s_encodeLookupTable[(val >> 12) & 0x3F];

            if (i + 1 < inLen) {
                output[j++] = s_encodeLookupTable[(val >> 6) & 0x3F];
            }
            else {
                output[j++] = '=';
            }
            output[j++] = '=';
        }

        return true;
    }

    bool decodeBase64(const std::string_view& input, std::string& output) {
        if (input.empty()) { return false; }

        const size_t inLen = input.size();
        size_t padding = 0;

        if (inLen >= 1 && input[inLen - 1] == '=') padding++;
        if (inLen >= 2 && input[inLen - 2] == '=') padding++;

        size_t outLen = (inLen / 4) * 3;
        if (outLen < padding) { return false; }

        outLen -= padding;
        output.resize(outLen);

        size_t j = 0;

        uint32_t val = 0;
        int valb = -8;

        for (const unsigned char c : input) {
            const unsigned char t = s_decodeLookupTable[c];

            if (t == 0xFF) {
                if (c == '=') { break; } // End of stream
                return false;            // Invalid char
            }

            val = (val << 6) + t;
            valb += 6;

            if (valb >= 0) {
                if (j < outLen) {
                    output[j++] = (char)((val >> valb) & 0xFF);
                }
                valb -= 8;
            }
        }

        return true;
    }
}