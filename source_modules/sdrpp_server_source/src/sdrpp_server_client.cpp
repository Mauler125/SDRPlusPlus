#include "sdrpp_server_client.h"
#include <volk/volk.h>
#include <cstring>
#include <utils/flog.h>
#include <core.h>
#include <utils/chacha20.h>
#include <utils/crc32.h>
#include <utils/crypto.h>
#include "utils/str_tools.h"

using namespace std::chrono_literals;

namespace server {
    Client::Client(std::shared_ptr<net::Socket> sock, dsp::stream<dsp::complex_t>* out, const uint8_t* key) : decompIn(false) {
        this->sock = sock;
        output = out;

        if (key) {
            memcpy(netKey, key, CHACHA20_KEY_LEN);
            useEncryption = true;
        }

        sendCryptoCtx.counter = 0;
        recvCryptoCtx.counter = 0;

        // Allocate buffers
        rbuffer = new uint8_t[SERVER_MAX_PACKET_SIZE];
        sbuffer = new uint8_t[SERVER_MAX_PACKET_SIZE];

        // Initialize headers
        r_pkt_hdr = (PacketHeader*)rbuffer;
        r_pkt_data = &rbuffer[sizeof(PacketHeader)];
        r_cmd_hdr = (CommandHeader*)r_pkt_data;
        r_cmd_data = &rbuffer[sizeof(PacketHeader) + sizeof(CommandHeader)];

        s_pkt_hdr = (PacketHeader*)sbuffer;
        s_pkt_data = &sbuffer[sizeof(PacketHeader)];
        s_cmd_hdr = (CommandHeader*)s_pkt_data;
        s_cmd_data = &sbuffer[sizeof(PacketHeader) + sizeof(CommandHeader)];

        // Initialize decompressor
        dctx = ZSTD_createDCtx();

        // Initialize DSP
        decompIn.setBufferSize(STREAM_BUFFER_SIZE*sizeof(dsp::complex_t) + 8);
        decompIn.clearWriteStop();
        decomp.init(&decompIn);
        link.init(&decomp.out, output);
        decomp.start();
        link.start();

        // Start worker thread
        workerThread = std::thread(&Client::worker, this);

        // Ask for a UI
        int res = getUI();
        if (res < 0) {
            // Close client
            close();

            // Throw error
            switch (res) {
            case CONN_ERR_TIMEOUT:
                throw std::runtime_error("Timed out");
            case CONN_ERR_BUSY:
                throw std::runtime_error("Server busy");
            case CONN_ERR_OVERFLOW:
                throw std::runtime_error("Buffer overflow");
            default:
                throw std::runtime_error("Unknown error");
            }
        }
    }

    Client::~Client() {
        close();

        link.shutdown();
        decomp.shutdown();

        ZSTD_freeDCtx(dctx);

        delete[] rbuffer;
        delete[] sbuffer;
    }

    void Client::showMenu() {
        std::string diffId = "";
        SmGui::DrawListElem diffValue;
        bool syncRequired = false;
        {
            std::lock_guard<std::mutex> lck(dlMtx);
            dl.draw(diffId, diffValue, syncRequired);
        }

        if (!diffId.empty()) {
            // Save ID
            SmGui::DrawListElem elemId;
            elemId.type = SmGui::DRAW_LIST_ELEM_TYPE_STRING;
            elemId.str = diffId;

            // Encore packet
            int size = 0;
            s_cmd_data[size++] = syncRequired;
            size += SmGui::DrawList::storeItem(elemId, &s_cmd_data[size], SERVER_MAX_PACKET_SIZE - size);
            size += SmGui::DrawList::storeItem(diffValue, &s_cmd_data[size], SERVER_MAX_PACKET_SIZE - size);

            // Send
            if (syncRequired) {
                flog::warn("Action requires resync");
                auto waiter = awaitCommandAck(COMMAND_UI_ACTION);
                sendCommand(COMMAND_UI_ACTION, size);
                if (waiter->await(PROTOCOL_TIMEOUT_MS)) {
                    std::lock_guard lck(dlMtx);
                    dl.load(r_cmd_data, r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
                }
                else {
                    flog::error("Timeout out after asking for UI");
                }
                waiter->handled();
                flog::warn("Resync done");
            }
            else {
                flog::warn("Action does not require resync");
                sendCommand(COMMAND_UI_ACTION, size);
            }
        }
    }

    void Client::setFrequency(double freq) {
        if (!isOpen()) { return; }
        *(double*)s_cmd_data = freq;
        sendCommand(COMMAND_SET_FREQUENCY, sizeof(double));
        auto waiter = awaitCommandAck(COMMAND_SET_FREQUENCY);
        waiter->await(PROTOCOL_TIMEOUT_MS);
        waiter->handled();
    }

    double Client::getSampleRate() {
        return currentSampleRate;
    }

    void Client::setSampleType(dsp::compression::PCMType type) {
        if (!isOpen()) { return; }
        s_cmd_data[0] = type;
        sendCommand(COMMAND_SET_SAMPLE_TYPE, 1);
    }

    void Client::setCompression(bool enabled) {
        if (!isOpen()) { return; }
        s_cmd_data[0] = enabled;
        sendCommand(COMMAND_SET_COMPRESSION, 1);
    }

    void Client::setEncryption(bool enabled) {
        // NOTE: disabling encryption is local only, server
        // needs to be restarted with it disabled!
        useEncryption = enabled;
    }

    void Client::start() {
        if (!isOpen()) { return; }
        sendCommand(COMMAND_START, 0);
        getUI();
    }

    void Client::stop() {
        if (!isOpen()) { return; }
        sendCommand(COMMAND_STOP, 0);
        getUI();
    }

    void Client::close() {
        // Stop worker
        decompIn.stopWriter();
        if (sock) { sock->close(); }
        if (workerThread.joinable()) { workerThread.join(); }
        decompIn.clearWriteStop();

        // Stop DSP
        link.stop();
        decomp.stop();
    }

    bool Client::isOpen() {
        return sock && sock->isOpen();
    }

    void Client::stopWaiters() {
        serverBusy = true;

        // Cancel waiters
        std::vector<PacketWaiter*> toBeRemoved;
        for (auto& [waiter, cmd] : commandAckWaiters) {
            waiter->cancel();
            toBeRemoved.push_back(waiter);
        }

        // Remove handled waiters
        for (auto& waiter : toBeRemoved) {
            commandAckWaiters.erase(waiter);
            delete waiter;
        }
    }

    void Client::worker() {
        while (true) {
            // Receive header
            if (sock->recv(rbuffer, sizeof(PacketHeader), true) <= 0) {
                stopWaiters();
                break;
            }

            // Check Magic
            if (r_pkt_hdr->magic != SERVER_PACKET_WIRE_MAGIC) {
                flog::error("Invalid packet magic: {0:X} != {1:X}", r_pkt_hdr->magic, SERVER_PACKET_WIRE_MAGIC);
                stopWaiters();
                break;
            }

            // Check Version
            if (r_pkt_hdr->version != SERVER_PROTOCOL_VERSION) {
                flog::error("Protocol version mismatch; server({0}) != client({1})", r_pkt_hdr->version, SERVER_PROTOCOL_VERSION);
                stopWaiters();
                break;
            }

            if (r_pkt_hdr->size > SERVER_MAX_PACKET_SIZE) {
                flog::error("Packet is too large; {0} > {1}", r_pkt_hdr->size, SERVER_MAX_PACKET_SIZE);
                stopWaiters();
                break;
            }

            // Receive remaining data
            if (sock->recv(&rbuffer[sizeof(PacketHeader)], r_pkt_hdr->size - sizeof(PacketHeader), true, PROTOCOL_TIMEOUT_MS) <= 0) {
                break;
            }

            // Increment data counter
            bytes += r_pkt_hdr->size;

            int payloadLen = r_pkt_hdr->size - sizeof(PacketHeader);

            // Decrypt
            if (useEncryption && (r_pkt_hdr->flags & PACKET_FLAGS_ENCRYPTED)) {
                ChaCha20_Init(&recvCryptoCtx, netKey, r_pkt_hdr->nonce, recvCryptoCtx.counter);
                ChaCha20_Xor(&recvCryptoCtx, r_pkt_data, payloadLen);

                recvCryptoCtx.counter++;
            }

            const uint32_t realChecksum = Crc32_Compute(r_pkt_data, payloadLen);

            if (r_pkt_hdr->checksum != realChecksum) {
                flog::error("Received corrupt packet (hdr->checksum({0}) != realChecksum({1})); disconnecting server.", r_pkt_hdr->checksum, realChecksum);
                stopWaiters();
            }

            // Decode packet
            if (r_pkt_hdr->type == PACKET_TYPE_COMMAND) {
                // TODO: Move to command handler
                if (r_cmd_hdr->cmd == COMMAND_SET_SAMPLERATE && payloadLen == sizeof(CommandHeader) + sizeof(double)) {
                    currentSampleRate = *(double*)r_cmd_data;
                    core::setInputSampleRate(currentSampleRate);
                }
                else if (r_cmd_hdr->cmd == COMMAND_DISCONNECT) {
                    flog::warn("Asked to disconnect by the server");
                    stopWaiters();
                }
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_COMMAND_ACK) {
                // Notify waiters
                std::vector<PacketWaiter*> toBeRemoved;
                for (auto& [waiter, cmd] : commandAckWaiters) {
                    if (cmd != r_cmd_hdr->cmd) { continue; }
                    waiter->notify();
                    toBeRemoved.push_back(waiter);
                }

                // Remove handled waiters
                for (auto& waiter : toBeRemoved) {
                    commandAckWaiters.erase(waiter);
                    delete waiter;
                }
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_BASEBAND) {
                if (r_pkt_hdr->flags & PACKET_FLAGS_COMPRESSED) {
                    size_t outCount = ZSTD_decompressDCtx(dctx, decompIn.writeBuf, STREAM_BUFFER_SIZE * sizeof(dsp::complex_t) + 8, r_pkt_data, payloadLen);
                    if (ZSTD_isError(outCount)) {
                        flog::error("Error decompressing baseband packet: {0}", ZSTD_getErrorName(outCount));
                    }
                    else if (outCount) {
                        if (!decompIn.swap(outCount)) { break; }
                    };
                }
                else {
                    memcpy(decompIn.writeBuf, r_pkt_data, payloadLen);
                    if (!decompIn.swap(payloadLen)) { break; }
                }
            }
            else if (r_pkt_hdr->type == PACKET_TYPE_ERROR) {
                r_pkt_data[4095] = '\0';
                flog::error("SDR++ Server Error: {0}", r_pkt_data[0]);
            }
            else {
                flog::error("Invalid packet type: {0}", r_pkt_hdr->type);
            }
        }
    }

    int Client::getUI() {
        if (!isOpen()) { return -1; }
        auto waiter = awaitCommandAck(COMMAND_GET_UI);
        sendCommand(COMMAND_GET_UI, 0);
        if (waiter->await(PROTOCOL_TIMEOUT_MS)) {
            std::lock_guard lck(dlMtx);

            if (r_pkt_hdr->size > SERVER_MAX_PACKET_SIZE) {
                flog::error("Packet is too large; {0} > {1}", r_pkt_hdr->size, SERVER_MAX_PACKET_SIZE);
                return CONN_ERR_OVERFLOW;
            }

            dl.load(r_cmd_data, r_pkt_hdr->size - sizeof(PacketHeader) - sizeof(CommandHeader));
        }
        else {
            if (!serverBusy) { flog::error("Timeout out after asking for UI"); };
            waiter->handled();
            return serverBusy ? CONN_ERR_BUSY : CONN_ERR_TIMEOUT;
        }
        waiter->handled();
        return 0;
    }

    void Client::sendPacket(PacketType type, int len) {
        s_pkt_hdr->magic = SERVER_PACKET_WIRE_MAGIC;
        s_pkt_hdr->version = SERVER_PROTOCOL_VERSION;
        s_pkt_hdr->type = type;
        s_pkt_hdr->flags = 0;
        s_pkt_hdr->checksum = Crc32_Compute(s_pkt_data, len);

        if (useEncryption) {
            Crypto_GenerateRandom(s_pkt_hdr->nonce, CHACHA20_IV_LEN);

            ChaCha20_Init(&sendCryptoCtx, netKey, s_pkt_hdr->nonce, sendCryptoCtx.counter);
            ChaCha20_Xor(&sendCryptoCtx, s_pkt_data, len);

            sendCryptoCtx.counter++;
            s_pkt_hdr->flags |= PACKET_FLAGS_ENCRYPTED;
        }

        s_pkt_hdr->size = sizeof(PacketHeader) + len;
        sock->send(sbuffer, s_pkt_hdr->size);
    }

    void Client::sendCommand(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        sendPacket(PACKET_TYPE_COMMAND, sizeof(CommandHeader) + len);
    }

    void Client::sendCommandAck(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        sendPacket(PACKET_TYPE_COMMAND_ACK, sizeof(CommandHeader) + len);
    }

    PacketWaiter* Client::awaitCommandAck(Command cmd) {
        PacketWaiter* waiter = new PacketWaiter;
        commandAckWaiters[waiter] = cmd;
        return waiter;
    }

    void Client::dHandler(dsp::complex_t *data, int count, void *ctx) {
        Client* _this = (Client*)ctx;
        memcpy(_this->output->writeBuf, data, count * sizeof(dsp::complex_t));
        _this->output->swap(count);
    }

    std::shared_ptr<Client> connect(const std::string& host, uint16_t port, dsp::stream<dsp::complex_t>* out, const uint8_t* key) {
        return std::make_shared<Client>(net::connect(host, port), out, key);
    }
}