#include "net.h"
#include <string.h>
#include <locale>
#include <codecvt>
#include <stdexcept>
#include <utils/flog.h>
#include <utils/str_tools.h>

namespace net {
    // === Address functions ===

    Address::Address() {
        clear();
    }

    Address::Address(const std::string& host) {
        // Initialize WSA if needed
        net::initLibrary();
        if (!setFromStr(host, true)) {
            throw std::runtime_error("Address initialization failed");
        }
    }

    Address::Address(const std::string& host, int port) {
        // Initialize WSA if needed
        net::initLibrary();
        if (!setFromStr("[" + host + "]:" + std::to_string(port), true)) {
            throw std::runtime_error("Address initialization failed");
        }
    }

    Address::Address(const IP_t& ip, int port) {
        clear();
        addr.sin6_addr = ip.addr;
        addr.sin6_port = htons(port);
    }

    void Address::clear() {
        memset(&addr, 0, sizeof(addr));
        addr.sin6_family = AF_INET6;
    }

    bool Address::setFromStr(const std::string& inStr, const bool useDNS) {
        clear();

        std::string copy = inStr;
        const size_t copyLen = copy.length();

        char* pCopy = &copy[0];
        bool hasBrackets = false;

        if (copy[0] == '[') // Skip bracket.
        {
            pCopy = &copy[1];
            hasBrackets = true;
        }

        char* snameSearchStart = pCopy;

        // Default it to false if we have brackets, and look
        // for the closing bracket. If we don't find the ']'
        // then the address string is incorrect, and we have
        // to assume there is no port number attached! If we
        // don't have brackets at all, always perform search!
        bool doServiceNameSearch = !hasBrackets;

        if (hasBrackets) {
            char* const bracketEnd = strchr(pCopy, ']');

            if (bracketEnd) {
                *bracketEnd = '\0'; // Remove the last bracket.
                snameSearchStart = &bracketEnd[1];

                doServiceNameSearch = true;
            }
        }

        const char* serviceName = nullptr;

        if (doServiceNameSearch) {
            char* const pchColon = strrchr(snameSearchStart, ':');

            // nb(kawe): only allow parsing of sname if the colon search points
            // to the same char in the buffer from both directions. If the user
            // provides "::FFFF:127.0.0.1:37015", do not search here because
            // it is not guaranteed that the reverse search of ':' will point
            // outside the address token (because forward search won't match
            // reverse search). If the user passed 127.0.0.1:37015, the search
            // will succeed. Incomplete addresses (i.e.[::FFFF:127.0.0.1:37015)
            // will also be dropped by the `doServiceNameSearch` condition above.
            // `snameSearchStart` will point to the port string "37015" in the
            // string [::FFFF:127.0.0.1]:37015 as the ']' and ':' gets trimmed.
            if (pchColon && strchr(snameSearchStart, ':') == pchColon) {
                pchColon[0] = '\0'; // Set the port.
                serviceName = &pchColon[1];

                if (utils::isAllDigit(serviceName)) // Service name is a port number.
                {
                    char* endPtr = nullptr;
                    const int32_t portNum = strtol(serviceName, &endPtr, 10);

                    if (*endPtr != '\0') {
                        flog::error("Short parse of port number in address '{0}'; consumed {1} bytes of {2} total!\n",
                                    inStr, (endPtr - serviceName), (&copy[copyLen] - serviceName));

                        return false;
                    }

                    if (portNum < 0 || portNum > 65535) {
                        flog::error("Invalid port number in address '{0}'; {1} lies outside range [{2},{3}]!\n", inStr, portNum, 0, 65535);
                        return false;
                    }

                    setPort(portNum);
                }
            }
        }

        // nb(kawe): at this stage, `pCopy` will be "127.0.0.1" (IPv4), or
        // "::FFFF:127.0.0.1" (IPv6). Scan for ':' to determine if we have IPv6.
        const bool ipv6Format = strchr(pCopy, ':') != nullptr;
        bool addrConvFailed = false;

        if (ipv6Format && isIPv6Syntax(pCopy)) {
            if (inet_pton(AF_INET6, pCopy, &addr.sin6_addr) == 1) {
                return true;
            }

            addrConvFailed = true;
        }
        else if (isIPv4Syntax(pCopy)) {
            char newAddressV4[128];
            snprintf(newAddressV4, sizeof(newAddressV4), "::FFFF:%s", pCopy);

            if (inet_pton(AF_INET6, newAddressV4, &addr.sin6_addr) == 1) {
                return true;
            }

            addrConvFailed = true;
        }

        // Perform DNS lookup on parsed result.
        if (useDNS) {
            addrinfo hints{};

            hints.ai_family = AF_INET6;
            hints.ai_flags = AI_ALL | AI_V4MAPPED;

            addrinfo* ppResult = nullptr;
            const int result = getaddrinfo(pCopy, serviceName, &hints, &ppResult);

            if (result != 0) {
                flog::error("Failed to resolve host name '{0}'; [{1}]\n", pCopy, gai_strerror(result));
                return false;
            }

            struct sockaddr_in6* const sock = (sockaddr_in6*)ppResult->ai_addr;
            setIP(sock->sin6_addr);

            freeaddrinfo(ppResult);
            return true;
        }
        else if (addrConvFailed) {
            flog::error("Failed to convert address '{0}'; [{1}]\n", pCopy, NET_ERROR());
        }

        return false;
    }

    std::string Address::toStr(const bool baseOnly) const {
        char buf[INET6_ADDRSTRLEN];
        const char* const ret = inet_ntop(AF_INET6, &addr.sin6_addr, buf, sizeof(buf));

        if (!ret) {
            throw std::runtime_error("Address deserialization failed");
            return std::string("-<[ErRoR]>-");
        }

        if (!baseOnly) {
            return "[" + std::string(buf) + "]:" + std::to_string(getPort());
        }

        return ret;
    }

    IP_t Address::getIP() const {
        return addr.sin6_addr;
    }

    void Address::setIP(const IP_t& ip) {
        addr.sin6_addr = ip.addr;
    }

    void Address::setIP(const uint32_t ip) {
        addr.sin6_addr.s6_addr[10] = 0xFF;
        addr.sin6_addr.s6_addr[11] = 0xFF;
        const uint32_t ipNbo = htonl(ip);
        memcpy(&addr.sin6_addr.s6_addr[12], &ipNbo, sizeof(uint32_t));
    }

    int Address::getPort() const {
        return ntohs(addr.sin6_port);
    }

    void Address::setPort(int port) {
        addr.sin6_port = htons(port);
    }

    // === Socket functions ===

    Socket::Socket(SockHandle_t sock, const Address* raddr) {
        this->sock = sock;
        if (raddr) {
            this->raddr = std::make_unique<Address>(*raddr);
        }
    }

    Socket::~Socket() {
        close();
    }

    void Socket::close() {
        if (!open) { return; }
        open = false;
        closeSocket(sock);
    }

    bool Socket::isOpen() {
        return open;
    }

    SocketType Socket::type() {
        return raddr ? SOCKET_TYPE_UDP : SOCKET_TYPE_TCP;
    }

    int Socket::send(const uint8_t* data, size_t len, const Address* dest) {
        sockaddr* const a = (sockaddr*)(dest ? &dest->addr : (raddr ? &raddr->addr : NULL));
        if (!a) { return -1; }

        // Send data
        const int err = sendto(sock, (const char*)data, len, 0, (sockaddr*)a, sizeof(sockaddr_in6));

        // On error, close socket
        if (err <= 0 && !WOULD_BLOCK) {
            close();
            return err;
        }

        return err;
    }

    int Socket::sendstr(const std::string& str, const Address* dest) {
        return send((const uint8_t*)str.c_str(), str.length(), dest);
    }

    int Socket::recv(uint8_t* data, size_t maxLen, bool forceLen, int timeout, Address* dest) {
        // Create FD set
        fd_set set;
        FD_ZERO(&set);
        
        int read = 0;
        bool blocking = (timeout != NONBLOCKING);
        do {
            // Wait for data or error if 
            if (blocking) {
                // Enable FD in set
                FD_SET(sock, &set);

                // Set timeout
                timeval tv;
                tv.tv_sec = timeout / 1000;
                tv.tv_usec = (timeout - tv.tv_sec*1000) * 1000;

                // Wait for data
                const int sel = select(sock + 1, &set, NULL, &set, (timeout >= 0) ? &tv : nullptr);
                if (sel < 0) {
                    if (!WOULD_BLOCK) { close(); }
                    return sel;
                }
                if (sel == 0) { return 0; }
            }

            // Receive
            int addrLen = sizeof(sockaddr_in6);
            int err = ::recvfrom(sock, (char*)&data[read], maxLen - read, 0, (sockaddr*)(dest ? &dest->addr : NULL), (socklen_t*)(dest ? &addrLen : NULL));

            // Peer has closed the connection
            if (err == 0) {
                close();
                return read;
            }

            // Error
            if (err < 0 && !WOULD_BLOCK) {
                close();
                return err;
            }
            read += err;
        }
        while (blocking && forceLen && read < maxLen);
        return read;
    }

    int Socket::recvline(std::string& str, int maxLen, int timeout, Address* dest) {
        // Disallow nonblocking mode
        if (!timeout) { return -1; }
        
        str.clear();
        int read = 0;
        while (!maxLen || read < maxLen) {
            char c;
            int err = recv((uint8_t*)&c, 1, false, timeout, dest);
            if (err <= 0) { return err; }
            read++;
            if (c == '\n') { break; }
            str += c;
        }
        return read;
    }

    // === Listener functions ===

    Listener::Listener(SockHandle_t sock) {
        this->sock = sock;
    }

    Listener::~Listener() {
        stop();
    }

    void Listener::stop() {
        closeSocket(sock);
        open = false;
    }

    bool Listener::listening() {
        return open;
    }

    std::shared_ptr<Socket> Listener::accept(Address* dest, int timeout) {
        // Create FD set
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);

        // Define timeout
        timeval tv{};
        timeval* tvPtr = NULL;

        // Blocking or timed wait
        if (timeout >= 0) {
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;
            tvPtr = &tv;
        }

        // Wait for data or error
        const int sel = select((int)(sock)+1, &readSet, NULL, &readSet, tvPtr);
        if (sel < 0) {
            if (!WOULD_BLOCK) { stop(); }
            return NULL;
        }

        // Timeout
        if (sel == 0) {
            return NULL;
        }

        socklen_t addrLen = sizeof(sockaddr_in6);
        sockaddr_in6* const addrPtr = dest ? &dest->addr : NULL;

        const SockHandle_t clientSock = ::accept(sock, (sockaddr*)(addrPtr), dest ? &addrLen : NULL);
        if (clientSock < 0) {
            if (!WOULD_BLOCK) { stop(); }
            return NULL;
        }

        setNonblocking(clientSock);
        return std::make_shared<Socket>(clientSock, dest);
    }

    // === Creation functions ===

    static in6_addr s_allNodesOnLink = { { 0xff, 0x02, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x01 } };

    std::map<std::string, InterfaceInfo> listInterfaces() {
        // Init library if needed
        net::initLibrary();
        std::map<std::string, InterfaceInfo> ifaces;

#ifdef _WIN32
        // Pre-allocate buffer
        ULONG size = sizeof(IP_ADAPTER_ADDRESSES);
        PIP_ADAPTER_ADDRESSES addresses = (PIP_ADAPTER_ADDRESSES)malloc(size);

        if (GetAdaptersAddresses(AF_UNSPEC, 0, NULL, addresses, &size) == ERROR_BUFFER_OVERFLOW) {
            // Reallocate to real size
            PIP_ADAPTER_ADDRESSES newBuf = (PIP_ADAPTER_ADDRESSES)realloc(addresses, size);

            if (!newBuf) {
                free(addresses);
                addresses = NULL;
            }
            else {
                addresses = newBuf;
            }
        }
        if (addresses && GetAdaptersAddresses(AF_UNSPEC, 0, NULL, addresses, &size)) {
            free(addresses);
            throw std::runtime_error("Could not list network interfaces");
        }

        // Save data
        std::wstring_convert<std::codecvt_utf8<wchar_t>> utfConv;
        for (auto iface = addresses; iface; iface = iface->Next) {
            for (auto unicast = iface->FirstUnicastAddress; unicast; unicast = unicast->Next) {
                if (!unicast->Address.lpSockaddr) {
                    continue;
                }

                InterfaceInfo& info = ifaces[utfConv.to_bytes(iface->FriendlyName)];
                info.family = unicast->Address.lpSockaddr->sa_family;

                if (info.family == AF_INET) {
                    const sockaddr_in* const sin = (sockaddr_in*)unicast->Address.lpSockaddr;
                    const uint32_t ip = ntohl(sin->sin_addr.s_addr);
                    const uint32_t mask = (unicast->OnLinkPrefixLength == 0)
                                              ? 0
                                              : (~0u << (32 - unicast->OnLinkPrefixLength));

                    info.address = IP_t(ip);
                    info.netmask = IP_t(htonl(mask));
                    info.broadcast = IP_t(ip | ~mask);
                }
                else if (info.family == AF_INET6) {
                    const sockaddr_in6* const sin6 = (sockaddr_in6*)unicast->Address.lpSockaddr;
                    info.address = IP_t(sin6->sin6_addr);

                    int bits = unicast->OnLinkPrefixLength;
                    for (int i = 0; i < 16; i++) {
                        if (bits >= 8) {
                            info.netmask.addr.s6_addr[i] = 0xFF;
                        }
                        else if (bits > 0) {
                            info.netmask.addr.s6_addr[i] = (0xFF << (8 - bits)) & 0xFF;
                        }
                        else {
                            info.netmask.addr.s6_addr[i] = 0x00;
                        }
                        bits -= 8;
                    }

                    info.broadcast = s_allNodesOnLink;
                }
            }
        }
        
        // Free tables
        free(addresses);
#else
        // Get iface list
        struct ifaddrs* addresses = NULL;
        getifaddrs(&addresses);

        // Save data
        for (auto iface = addresses; iface; iface = iface->ifa_next) {
            if (!iface->ifa_addr || !iface->ifa_netmask) {
                continue;
            }

            InterfaceInfo& info = ifaces[iface->ifa_name];
            info.family = iface->ifa_addr->sa_family;

            if (info.family == AF_INET) {
                const sockaddr_in* const addr = (sockaddr_in*)iface->ifa_addr;
                const sockaddr_in* const mask = (sockaddr_in*)iface->ifa_netmask;

                const uint32_t ip = ntohl(addr->sin_addr.s_addr);
                const uint32_t nm = ntohl(mask->sin_addr.s_addr);

                info.address = IP_t(ip);
                info.netmask = IP_t(htonl(nm));
                info.broadcast = IP_t(ip | ~nm);
            }
            else if (info.family == AF_INET6) {
                const sockaddr_in6* const addr6 = (sockaddr_in6*)iface->ifa_addr;
                const sockaddr_in6* const mask6 = (sockaddr_in6*)iface->ifa_netmask;

                info.address = IP_t(addr6->sin6_addr);
                info.netmask = IP_t(mask6->sin6_addr);
                info.broadcast = s_allNodesOnLink;
            }
        }

        // Free iface list
        freeifaddrs(addresses);
#endif

        return ifaces;
    }

    std::shared_ptr<Listener> listen(const Address& addr) {
        // Init library if needed
        if (!net::initLibrary()) {
            return NULL;
        }

        // Create socket
        SockHandle_t s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
        // TODO: Support non-blockign mode

#ifndef _WIN32
        // Allow port reusing if the app was killed or crashed
        // and the socket is stuck in TIME_WAIT state.
        // This option has a different meaning on Windows,
        // so we use it only for non-Windows systems
        int enable = 1;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            closeSocket(s);
            throw std::runtime_error("Could not enable port reuse on socket");
            return NULL;
        }
#endif

        int opt = 0; // Disable IPv6 only mode (support IPv4 as well)
        if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&opt, sizeof(opt)) < 0) {
            closeSocket(s);
            throw std::runtime_error("Could not enable dual stack on socket");
            return NULL;
        }

        // Bind socket to the port
        if (bind(s, (sockaddr*)&addr.addr, sizeof(sockaddr_in6))) {
            closeSocket(s);
            throw std::runtime_error("Could not bind socket");
            return NULL;
        }

        // Enable listening
        if (::listen(s, SOMAXCONN) != 0) {
            throw std::runtime_error("Could start listening for connections");
            return NULL;
        }

        // Enable nonblocking mode
        setNonblocking(s);

        // Return listener class
        return std::make_shared<Listener>(s);
    }

    std::shared_ptr<Listener> listen(const std::string& host, int port) {
        return listen(Address(host, port));
    }

    std::shared_ptr<Socket> connect(const Address& addr) {
        // Init library if needed
        if (!net::initLibrary()) {
            return NULL;
        }

        // Create socket
        SockHandle_t s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

        // Connect to server
        if (::connect(s, (sockaddr*)&addr.addr, sizeof(sockaddr_in6))) {
            closeSocket(s);
            throw std::runtime_error("Could not connect");
            return NULL;
        }

        // Enable nonblocking mode
        setNonblocking(s);

        // Return socket class
        return std::make_shared<Socket>(s);
    }

    std::shared_ptr<Socket> connect(const std::string& host, int port) {
        return connect(Address(host, port));
    }

    std::shared_ptr<Socket> openudp(const Address& raddr, const Address& laddr, bool allowBroadcast) {
        // Init library if needed
        if (!net::initLibrary()) {
            return NULL;
        }

        // Create socket
        SockHandle_t s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

        int opt = 0; // Disable IPv6 only mode (support IPv4 as well)
        if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&opt, sizeof(opt)) < 0) {
            closeSocket(s);
            throw std::runtime_error("Could not enable dual stack on socket");
            return NULL;
        }

        // If the remote address is multicast, allow multicast connections
        int enable = allowBroadcast;
        if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char*)&enable, sizeof(int)) < 0) {
            closeSocket(s);
            throw std::runtime_error("Could not enable broadcast on socket");
            return NULL;
        }

        // Bind socket to local port
        if (bind(s, (sockaddr*)&laddr.addr, sizeof(sockaddr_in6))) {
            closeSocket(s);
            throw std::runtime_error("Could not bind socket");
            return NULL;
        }
        
        // Return socket class
        return std::make_shared<Socket>(s, &raddr);
    }

    std::shared_ptr<Socket> openudp(const std::string& rhost, int rport, const Address& laddr, bool allowBroadcast) {
        return openudp(Address(rhost, rport), laddr, allowBroadcast);
    }

    std::shared_ptr<Socket> openudp(const Address& raddr, const std::string& lhost, int lport, bool allowBroadcast) {
        return openudp(raddr, Address(lhost, lport), allowBroadcast);
    }

    std::shared_ptr<Socket> openudp(const std::string& rhost, int rport, const std::string& lhost, int lport, bool allowBroadcast) {
        return openudp(Address(rhost, rport), Address(lhost, lport), allowBroadcast);
    }
}
