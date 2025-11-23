#pragma once
#include "net_shared.h"

namespace net {
    struct ConnReadEntry {
        int count;
        uint8_t* buf;
        void (*handler)(int count, uint8_t* buf, void* ctx);
        void* ctx;
        bool enforceSize;
    };

    struct ConnWriteEntry {
        int count;
        uint8_t* buf;
    };

    class ConnClass {
    public:
        ConnClass(SockHandle_t sock, struct sockaddr_in6 raddr = {}, bool udp = false);
        ~ConnClass();

        void close();
        bool isOpen();
        void waitForEnd();

        size_t toString(char* const buffer, const size_t bufferSize, const bool onlyBase = false) const;

        int read(int count, uint8_t* buf, bool enforceSize = true);
        bool write(int count, uint8_t* buf);
        void readAsync(int count, uint8_t* buf, void (*handler)(int count, uint8_t* buf, void* ctx), void* ctx, bool enforceSize = true);
        void writeAsync(int count, uint8_t* buf);

    private:
        void readWorker();
        void writeWorker();

        std::atomic_bool stopWorkers = false;
        std::atomic_bool connectionOpen = false;
        bool _udp;
        SockHandle_t _sock;
        struct sockaddr_in6 remoteAddr;
        std::mutex readMtx;
        std::mutex writeMtx;
        std::mutex readQueueMtx;
        std::mutex writeQueueMtx;
        std::mutex connectionOpenMtx;
        std::mutex closeMtx;
        std::condition_variable readQueueCnd;
        std::condition_variable writeQueueCnd;
        std::condition_variable connectionOpenCnd;
        std::vector<ConnReadEntry> readQueue;
        std::vector<ConnWriteEntry> writeQueue;
        std::thread readWorkerThread;
        std::thread writeWorkerThread;
    };

    typedef std::unique_ptr<ConnClass> Conn;

    struct ListenerAcceptEntry {
        void (*handler)(Conn conn, void* ctx);
        void* ctx;
    };

    class ListenerClass {
    public:
        ListenerClass(SockHandle_t listenSock);
        ~ListenerClass();

        Conn accept();
        void acceptAsync(void (*handler)(Conn conn, void* ctx), void* ctx);

        void close();
        bool isListening();

    private:
        void worker();

        std::atomic_bool stopWorker = false;
        std::atomic_bool listening = false;

        SockHandle_t sock;

        std::mutex acceptMtx;
        std::mutex acceptQueueMtx;
        std::condition_variable acceptQueueCnd;
        std::vector<ListenerAcceptEntry> acceptQueue;
        std::thread acceptWorkerThread;
    };

    typedef std::unique_ptr<ListenerClass> Listener;

    Conn connect(std::string host, uint16_t port);
    Listener listen(std::string host, uint16_t port);
    Conn openUDP(std::string host, uint16_t port, std::string remoteHost, uint16_t remotePort, bool bindSocket = true);

#ifdef _WIN32
    extern bool winsock_init;
#endif
}