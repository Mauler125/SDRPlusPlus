#include <utils/networking.h>
#include <assert.h>
#include <utils/flog.h>
#include <stdexcept>

namespace net {
#ifdef _WIN32
    extern bool winsock_init = false;
#endif

    ConnClass::ConnClass(const SockHandle_t sock, struct sockaddr_in6 raddr, const bool udp) {
        _sock = sock;
        _udp = udp;
        remoteAddr = raddr;
        connectionOpen = true;
        readWorkerThread = std::thread(&ConnClass::readWorker, this);
        writeWorkerThread = std::thread(&ConnClass::writeWorker, this);
    }

    ConnClass::~ConnClass() {
        ConnClass::close();
    }

    void ConnClass::close() {
        std::lock_guard lck(closeMtx);
        // Set stopWorkers to true
        {
            std::lock_guard lck1(readQueueMtx);
            std::lock_guard lck2(writeQueueMtx);
            stopWorkers = true;
        }

        // Notify the workers of the change
        readQueueCnd.notify_all();
        writeQueueCnd.notify_all();

        if (connectionOpen) {
            net::closeSocket(_sock);
        }

        // Wait for the theads to terminate
        if (readWorkerThread.joinable()) { readWorkerThread.join(); }
        if (writeWorkerThread.joinable()) { writeWorkerThread.join(); }

        {
            std::lock_guard lck(connectionOpenMtx);
            connectionOpen = false;
        }
        connectionOpenCnd.notify_all();
    }

    bool ConnClass::isOpen() {
        return connectionOpen;
    }

    void ConnClass::waitForEnd() {
        std::unique_lock lck(readQueueMtx);
        connectionOpenCnd.wait(lck, [this]() { return !connectionOpen; });
    }

    size_t ConnClass::toString(char* const buffer, const size_t bufferSize, const bool onlyBase) const {
        assert(bufferSize > 0);
        char stringBuf[INET6_ADDRSTRLEN];

        if (inet_ntop(AF_INET6, &remoteAddr.sin6_addr, stringBuf, INET6_ADDRSTRLEN)) {
            const int ret = onlyBase
                                ? snprintf(buffer, bufferSize, "%s", stringBuf)
                                : snprintf(buffer, bufferSize, "[%s]:%hu", stringBuf, (uint16_t)ntohs(remoteAddr.sin6_port));

            if (ret > 0) {
                return std::min<size_t>(static_cast<size_t>(ret), bufferSize - 1);
            }
        }
        const size_t len = std::min<size_t>(sizeof("-<[ErRoR]>-"), bufferSize) - 1;
        memcpy(buffer, "-<[ErRoR]>-", len);
        buffer[len] = '\0';
        return len;
    }

    int ConnClass::read(int count, uint8_t* buf, bool enforceSize) {
        if (!connectionOpen) { return -1; }
        std::lock_guard lck(readMtx);
        int ret;

        if (_udp) {
            socklen_t fromLen = sizeof(remoteAddr);
            ret = recvfrom(_sock, (char*)buf, count, 0, (struct sockaddr*)&remoteAddr, &fromLen);
            if (ret <= 0) {
                {
                    std::lock_guard lck(connectionOpenMtx);
                    connectionOpen = false;
                }
                connectionOpenCnd.notify_all();
                return -1;
            }
            return count; // todo(kawe): is this correct, or should we return `ret`?
        }

        int beenRead = 0;
        while (beenRead < count) {
            ret = recv(_sock, (char*)&buf[beenRead], count - beenRead, 0);

            if (ret <= 0) {
                {
                    std::lock_guard lck(connectionOpenMtx);
                    connectionOpen = false;
                }
                connectionOpenCnd.notify_all();
                return -1;
            }

            if (!enforceSize) { return ret; }

            beenRead += ret;
        }

        return beenRead;
    }

    bool ConnClass::write(int count, uint8_t* buf) {
        if (!connectionOpen) { return false; }
        std::lock_guard lck(writeMtx);
        int ret;

        if (_udp) {
            ret = sendto(_sock, (char*)buf, count, 0, (struct sockaddr*)&remoteAddr, sizeof(remoteAddr));
            if (ret <= 0) {
                {
                    std::lock_guard lck(connectionOpenMtx);
                    connectionOpen = false;
                }
                connectionOpenCnd.notify_all();
            }
            return (ret > 0);
        }

        int beenWritten = 0;
        while (beenWritten < count) {
            ret = send(_sock, (char*)buf + beenWritten, count - beenWritten, 0);
            if (ret <= 0) {
                {
                    std::lock_guard lck(connectionOpenMtx);
                    connectionOpen = false;
                }
                connectionOpenCnd.notify_all();
                return false;
            }
            beenWritten += ret;
        }
        
        return true;
    }

    void ConnClass::readAsync(int count, uint8_t* buf, void (*handler)(int count, uint8_t* buf, void* ctx), void* ctx, bool enforceSize) {
        if (!connectionOpen) { return; }
        // Create entry
        ConnReadEntry entry;
        entry.count = count;
        entry.buf = buf;
        entry.handler = handler;
        entry.ctx = ctx;
        entry.enforceSize = enforceSize;

        // Add entry to queue
        {
            std::lock_guard lck(readQueueMtx);
            readQueue.push_back(entry);
        }

        // Notify read worker
        readQueueCnd.notify_all();
    }

    void ConnClass::writeAsync(int count, uint8_t* buf) {
        if (!connectionOpen) { return; }
        // Create entry
        ConnWriteEntry entry;
        entry.count = count;
        entry.buf = buf;

        // Add entry to queue
        {
            std::lock_guard lck(writeQueueMtx);
            writeQueue.push_back(entry);
        }

        // Notify write worker
        writeQueueCnd.notify_all();
    }

    void ConnClass::readWorker() {
        while (true) {
            // Wait for wakeup and exit if it's for terminating the thread
            std::unique_lock lck(readQueueMtx);
            readQueueCnd.wait(lck, [this]() { return (readQueue.size() > 0 || stopWorkers); });
            if (stopWorkers || !connectionOpen) { return; }

            // Pop first element off the list
            ConnReadEntry entry = readQueue[0];
            readQueue.erase(readQueue.begin());
            lck.unlock();

            // Read from socket and send data to the handler
            int ret = read(entry.count, entry.buf, entry.enforceSize);
            if (ret <= 0) {
                {
                    std::lock_guard lck(connectionOpenMtx);
                    connectionOpen = false;
                }
                connectionOpenCnd.notify_all();
                return;
            }
            entry.handler(ret, entry.buf, entry.ctx);
        }
    }

    void ConnClass::writeWorker() {
        while (true) {
            // Wait for wakeup and exit if it's for terminating the thread
            std::unique_lock lck(writeQueueMtx);
            writeQueueCnd.wait(lck, [this]() { return (writeQueue.size() > 0 || stopWorkers); });
            if (stopWorkers || !connectionOpen) { return; }

            // Pop first element off the list
            ConnWriteEntry entry = writeQueue[0];
            writeQueue.erase(writeQueue.begin());
            lck.unlock();

            // Write to socket
            if (!write(entry.count, entry.buf)) {
                {
                    std::lock_guard lck(connectionOpenMtx);
                    connectionOpen = false;
                }
                connectionOpenCnd.notify_all();
                return;
            }
        }
    }


    ListenerClass::ListenerClass(SockHandle_t listenSock) {
        sock = listenSock;
        listening = true;
        acceptWorkerThread = std::thread(&ListenerClass::worker, this);
    }

    ListenerClass::~ListenerClass() {
        close();
    }

    Conn ListenerClass::accept() {
        if (!listening) { return NULL; }
        std::lock_guard lck(acceptMtx);

        sockaddr_in6 addr{};
        socklen_t addrLen = sizeof(addr);

        // Accept socket
        const SockHandle_t _sock = ::accept(sock, (sockaddr*)&addr, &addrLen);
        if (_sock < 0) {
            listening = false;
            throw std::runtime_error("Could not bind socket");
            return NULL;
        }

        return Conn(new ConnClass(_sock, addr));
    }

    void ListenerClass::acceptAsync(void (*handler)(Conn conn, void* ctx), void* ctx) {
        if (!listening) { return; }
        // Create entry
        ListenerAcceptEntry entry;
        entry.handler = handler;
        entry.ctx = ctx;

        // Add entry to queue
        {
            std::lock_guard lck(acceptQueueMtx);
            acceptQueue.push_back(entry);
        }

        // Notify write worker
        acceptQueueCnd.notify_all();
    }

    void ListenerClass::close() {
        {
            std::lock_guard lck(acceptQueueMtx);
            stopWorker = true;
        }
        acceptQueueCnd.notify_all();

        if (listening) {
            net::closeSocket(sock);
        }

        if (acceptWorkerThread.joinable()) { acceptWorkerThread.join(); }


        listening = false;
    }

    bool ListenerClass::isListening() {
        return listening;
    }

    void ListenerClass::worker() {
        while (true) {
            // Wait for wakeup and exit if it's for terminating the thread
            std::unique_lock lck(acceptQueueMtx);
            acceptQueueCnd.wait(lck, [this]() { return (acceptQueue.size() > 0 || stopWorker); });
            if (stopWorker || !listening) { return; }

            // Pop first element off the list
            ListenerAcceptEntry entry = acceptQueue[0];
            acceptQueue.erase(acceptQueue.begin());
            lck.unlock();

            // Read from socket and send data to the handler
            try {
                Conn client = accept();
                if (!client) {
                    listening = false;
                    return;
                }
                entry.handler(std::move(client), entry.ctx);
            }
            catch (const std::exception& e) {
                listening = false;
                return;
            }
        }
    }

    static bool doSystemInit() {
#ifdef _WIN32
        // Initialize WinSock2
        if (!winsock_init) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa)) {
                throw std::runtime_error("Could not initialize WinSock2");
                return false;
            }
            winsock_init = true;
        }
        assert(winsock_init);
#else
        signal(SIGPIPE, SIG_IGN);
#endif
        return true;
    }

    static SockHandle_t createSocket(const int sockType, const IPPROTO protocol, const bool reuse) {
        const SockHandle_t sock = socket(AF_INET6, sockType, protocol);
        if (sock < 0) {
            throw std::runtime_error("Could not create socket");
            return sock;
        }

        if (reuse) {
            // Allow port reusing if the app was killed or crashed
            // and the socket is stuck in TIME_WAIT state.
            // This option has a different meaning on Windows,
            // so we use it only for non-Windows systems
            int opt = 1;
            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) {
                net::closeSocket(sock);
                throw std::runtime_error("Could not enable port reuse on socket");
                return NULL;
            }
        }

        int opt = 0; // Disable IPv6 only mode (support IPv4 as well)
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&opt, sizeof(opt)) < 0) {
            net::closeSocket(sock);
            throw std::runtime_error("Could not enable dual stack on socket");
            return sock;
        }

        return sock;
    }

    static addrinfo* getAddrInfo(const std::string& hostName, const std::string& serviceName, const int sockType, const IPPROTO protocol) {
        addrinfo hints{};
        hints.ai_flags = AI_PASSIVE | AI_ALL | AI_V4MAPPED;
        hints.ai_family = AF_INET6;
        hints.ai_socktype = sockType;
        hints.ai_protocol = protocol;

        addrinfo* out = nullptr;
        const int ret = getaddrinfo(hostName.c_str(), serviceName.c_str(), &hints, &out);

        if (ret != 0) {
            return NULL;
        }

        return out;
    }

    static bool bindToSock(const addrinfo* const addrInfo, const SockHandle_t sockHnd) {
        for (const addrinfo* rp = addrInfo; rp != nullptr; rp = rp->ai_next) {
            if (::bind(sockHnd, rp->ai_addr, rp->ai_addrlen) == 0) {
                return true;
            }
        }

        return false;
    }

    static bool connToSock(const addrinfo* const addrInfo, const SockHandle_t sockHnd) {
        for (const addrinfo* rp = addrInfo; rp != nullptr; rp = rp->ai_next) {
            if (::connect(sockHnd, rp->ai_addr, rp->ai_addrlen) == 0) {
                return true;
            }
        }

        return false;
    }

    Conn connect(const std::string& host, const uint16_t port) {
        if (!doSystemInit()) {
            return NULL;
        }

        // Create a socket
        const SockHandle_t sock = createSocket(SOCK_STREAM, IPPROTO_TCP, false);

        if (sock < 0) {
            return NULL;
        }

        addrinfo* const result = getAddrInfo(host, std::to_string(port), SOCK_STREAM, IPPROTO_TCP);
        if (!result) {
            net::closeSocket(sock);
            throw std::runtime_error("Could not resolve host name");
            return NULL;
        }

        const bool connectSuccess = connToSock(result, sock);
        freeaddrinfo(result);

        if (!connectSuccess) {
            net::closeSocket(sock);
            throw std::runtime_error("Could not connect to host");
            return NULL;
        }

        return Conn(new ConnClass(sock));
    }

    Listener listen(const std::string& host, const uint16_t port) {
        if (!doSystemInit()) {
            return NULL;
        }

        // Create a socket
        const SockHandle_t listenSock = createSocket(SOCK_STREAM, IPPROTO_TCP, !_WIN32);

        if (listenSock < 0) {
            return NULL;
        }

        addrinfo* const result = getAddrInfo(host, std::to_string(port), SOCK_STREAM, IPPROTO_TCP);

        if (!result) {
            net::closeSocket(listenSock);
            throw std::runtime_error("Could not resolve host name");
            return NULL;
        }

        const bool bindSuccess = bindToSock(result, listenSock);
        freeaddrinfo(result);

        if (!bindSuccess) {
            net::closeSocket(listenSock);
            throw std::runtime_error("Could not bind socket");
            return NULL;
        }

        // Listen
        if (::listen(listenSock, SOMAXCONN) != 0) {
            net::closeSocket(listenSock);
            throw std::runtime_error("Could not listen");
            return NULL;
        }

        return Listener(new ListenerClass(listenSock));
    }

    Conn openUDP(const std::string& host,
                 const uint16_t port,
                 const std::string& remoteHost,
                 const uint16_t remotePort,
                 const bool bindSocket) {
        if (!doSystemInit()) {
            return NULL;
        }

        // Create a socket
        const SockHandle_t sock = createSocket(SOCK_DGRAM, IPPROTO_UDP, false);
        if (sock < 0) {
            return NULL;
        }

        // Get address from local hostname/ip
        addrinfo* const resultLocal = getAddrInfo(host, std::to_string(port), SOCK_DGRAM, IPPROTO_UDP);

        if (!resultLocal) {
            net::closeSocket(sock);
            throw std::runtime_error("Could not resolve local host name");
            return NULL;
        }

        // Get address from remote hostname/ip
        addrinfo* const resultRemote = getAddrInfo(remoteHost, std::to_string(remotePort), SOCK_DGRAM, IPPROTO_UDP);

        if (!resultRemote) {
            freeaddrinfo(resultLocal);
            net::closeSocket(sock);
            throw std::runtime_error("Could not resolve remote host name");
            return NULL;
        }

        // Bind host address
        if (bindSocket) {
            if (!bindToSock(resultLocal, sock)) {
                freeaddrinfo(resultLocal);
                freeaddrinfo(resultRemote);
                net::closeSocket(sock);
                throw std::runtime_error("Could not bind socket");
                return NULL;
            }
        }

        ConnClass* const toRet = new ConnClass(sock, *(sockaddr_in6*)resultRemote->ai_addr, true);

        freeaddrinfo(resultLocal);
        freeaddrinfo(resultRemote);

        return Conn(toRet);
    }
}