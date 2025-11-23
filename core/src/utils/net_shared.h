#pragma once
#include <stdint.h>
#include <inttypes.h>
#include <string>
#include <cstring>
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include <thread>
#include <condition_variable>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>
#else
#include <unistd.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <ifaddrs.h>
#endif

#ifdef _WIN32
#define NET_ERROR() WSAGetLastError()
#define WOULD_BLOCK (NET_ERROR() == WSAEWOULDBLOCK)
#else
#include <arpa/inet.h>
#define NET_ERROR() errno
#define WOULD_BLOCK (NET_ERROR() == EWOULDBLOCK)
#endif

namespace net {
#ifdef _WIN32
    typedef SOCKET SockHandle_t;
    typedef int socklen_t;
#else
    typedef int SockHandle_t;
#endif
    typedef struct IP_s {
        IP_s() {
            addr = {};
        }
        IP_s(const in6_addr& ip) {
            addr = ip;
        }
        IP_s(const uint32_t ip) {
            addr.s6_addr[10] = 0xFF;
            addr.s6_addr[11] = 0xFF;
            const uint32_t ipNbo = htonl(ip);
            memcpy(&addr.s6_addr[12], &ipNbo, sizeof(uint32_t));
        }

        in6_addr addr;
    } IP_t;

    inline static std::atomic_bool s_libraryInitialized = false;

    inline bool initLibrary() {
#ifdef _WIN32
        // Initialize WinSock2
        if (!s_libraryInitialized.exchange(true)) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
                throw std::runtime_error("Could not initialize WinSock2");
                return false;
            }
        }
#else
        signal(SIGPIPE, SIG_IGN);
#endif
        return true;
    }

    inline void closeSocket(SockHandle_t sock) {
#ifdef _WIN32
        shutdown(sock, SD_BOTH);
        closesocket(sock);
#else
        shutdown(sock, SHUT_RDWR);
        close(sock);
#endif
    }

    inline void setNonblocking(SockHandle_t sock) {
#ifdef _WIN32
        u_long enabled = 1;
        ioctlsocket(sock, FIONBIO, &enabled);
#else
        fcntl(sock, F_SETFL, O_NONBLOCK);
#endif
    }

    inline bool isIPv4Syntax(const char* const address) {
        if (!*address) {
            return true;
        }

        for (const uint8_t* p = (const uint8_t*)address; const uint8_t c = *p; ++p) {
            if (c >= '0' && c <= '9') {
                continue;
            }
            if (c == '.') {
                continue;
            }
            return false;
        }

        return true;
    }

    inline bool isIPv6Syntax(const char* const address) {
        if (!*address) {
            return true;
        }

        // Only allow non-hexadecimal characters if we encountered
        // a zone index delimiter. It can only contain 1. See ref:
        // https://datatracker.ietf.org/doc/html/rfc4007
        bool inZoneId = false;
        uint8_t prev = '\0';

        for (const uint8_t* p = (const uint8_t*)address; const uint8_t c = *p; ++p) {
            prev = c;

            if (c == ':' || c == '.') continue;
            if (!inZoneId && c == '%') {
                inZoneId = true;
                continue;
            }
            if (c >= '0' && c <= '9') {
                continue;
            }
            if (c >= 'A' && c <= 'F') {
                continue;
            }
            if (c >= 'a' && c <= 'f') {
                continue;
            }

            if (inZoneId) {
                if (c == '/' || c == '_' || c == '-') {
                    continue;
                }
                if (c >= 'A' && c <= 'Z') {
                    continue;
                }
                if (c >= 'a' && c <= 'z') {
                    continue;
                }
            }

            return false;
        }

        // Trailing delim isn't valid, must have a zone id!
        return prev != '%';
    }
}