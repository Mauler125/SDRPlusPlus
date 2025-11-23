#pragma once
#include <charconv>

namespace utils {
    // this is locale-unaware and therefore faster version of standard isdigit()
    // It also avoids sign-extension errors.
    inline bool isDigit(const char c) {
        return c >= '0' && c <= '9';
    }

    inline bool isWDigit(const int c) {
        return (((unsigned int)(c - '0')) < 10);
    }

    inline bool isPunct(const int c) {
        if (c >= 33 && c <= 126) {
            if ((c >= '0' && c <= '9') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z')) {
                return false;
            }

            // All other printable ASCII are punctuation.
            return true;
        }

        return false;
    }

    inline bool isAllDigit(const char* pString) {
        while (*pString) {
            if (!isDigit(*pString)) {
                return false;
            }

            pString++;
        }

        return true;
    }

    inline unsigned char getNibble(const char c) {
        if ((c >= '0') &&
            (c <= '9')) {
            return (unsigned char)(c - '0');
        }

        if ((c >= 'A') &&
            (c <= 'F')) {
            return (unsigned char)(c - 'A' + 0x0a);
        }

        if ((c >= 'a') &&
            (c <= 'f')) {
            return (unsigned char)(c - 'a' + 0x0a);
        }

        return '0';
    }

    template <class V>
    inline bool strToNum(const char* const str, const size_t len, V& num) {
        const char* const end = &str[len];
        std::from_chars_result result;

        if constexpr ((std::is_same<V, int32_t>::value) || (std::is_same<V, int64_t>::value) ||
                      (std::is_same<V, uint32_t>::value) || (std::is_same<V, uint64_t>::value)) {
            int ofs = 0;
            int base = 10;

            // Note: char_conv doesn't auto detect the base, and it expects the
            // numeric string without the "0x" (base16) or "0b" (base8) prefix.
            if (len > 1) {
                if (str[0] == '0') {
                    if (str[1] == 'x' || str[1] == 'X') {
                        ofs = 2; // Hexadecimal, skip "0x".
                        base = 16;
                    }
                    else if (str[1] == 'b' || str[1] == 'B') {
                        ofs = 2; // Binary, skip "0b".
                        base = 2;
                    }
                    else { // Octal doesn't need skipping.
                        base = 8;
                    }
                }
            }

            result = std::from_chars(&str[ofs], end, num, base);
        }
        else if constexpr ((std::is_same<V, float>::value) || (std::is_same<V, double>::value)) {
            result = std::from_chars(str, end, num, std::chars_format::general);
        }
        else {
            static_assert(std::is_same_v<V, void>, "Cannot classify numeric type; unsupported.");
        }

        return (result.ec == std::errc()) && (result.ptr == end);
    }

    template <class V>
    inline bool strToNum(const std::string& str, V& num) {
        return strToNum<V>(str.c_str(), str.length(), num);
    }
}