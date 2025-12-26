#include "net_shared.h"

inline static std::atomic_bool s_libraryInitialized = false;

namespace net {
    bool initLibrary() {
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

    bool shutdownLibrary() {
#ifdef _WIN32
        if (s_libraryInitialized.exchange(false)) {
            if (WSACleanup()) {
                throw std::runtime_error("Could not shutdown WinSock2");
                return false;
            }
        }
#endif
        return true;
    }
}