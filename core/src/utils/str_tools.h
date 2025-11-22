#pragma once

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
}