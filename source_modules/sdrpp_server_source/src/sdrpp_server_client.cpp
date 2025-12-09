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

        resetCryptoCounters();

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
                break;
            case CONN_ERR_BUSY:
                throw std::runtime_error("Server busy");
                break;
            case CONN_ERR_OVERFLOW:
                throw std::runtime_error("Buffer overflow");
                break;
            case CONN_ERR_SEND_FAIL:
                throw std::runtime_error("Send failure");
                break;
            default:
                throw std::runtime_error("Unknown error");
                break;
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

    bool Client::setFrequency(double freq) {
        if (!isOpen()) { return false; }
        *(double*)s_cmd_data = freq;
        if (!sendCommand(COMMAND_SET_FREQUENCY, sizeof(double))) {
            return false;
        }
        auto waiter = awaitCommandAck(COMMAND_SET_FREQUENCY);
        waiter->await(PROTOCOL_TIMEOUT_MS);
        waiter->handled();
        return true;
    }

    double Client::getSampleRate() {
        return currentSampleRate;
    }

    bool Client::setSampleType(dsp::compression::PCMType type) {
        if (!isOpen()) { return false; }
        s_cmd_data[0] = type;
        return sendCommand(COMMAND_SET_SAMPLE_TYPE, 1);
    }

    bool Client::setCompression(bool enabled) {
        if (!isOpen()) { return false; }
        s_cmd_data[0] = enabled;
        return sendCommand(COMMAND_SET_COMPRESSION, 1);
    }

    void Client::setEncryption(bool enabled) {
        // NOTE: disabling encryption is local only, server
        // needs to be restarted with it disabled!
        useEncryption = enabled;
    }

    bool Client::start() {
        if (!isOpen()) { return false; }
        if (!sendCommand(COMMAND_START, 0)) {
            return false;
        }
        return getUI() == 0;
    }

    bool Client::stop() {
        if (!isOpen()) { return false; }
        if (!sendCommand(COMMAND_STOP, 0)) {
            return false;
        }
        return getUI() == 0;
    }

    void Client::close() {
        resetCryptoCounters();

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

    static uint64_t getTimeUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void Client::worker() {
        while (true) {
            // Receive header
            if (sock->recv(rbuffer, sizeof(PacketHeader), true) <= 0) {
                stopWaiters();
                break;
            }

            if (r_pkt_hdr->magic != SERVER_PACKET_WIRE_MAGIC) {
                flog::error("Packet has bad magic (expected {0}, got {1})", SERVER_PACKET_WIRE_MAGIC, r_pkt_hdr->magic);
                stopWaiters();
                break;
            }

            if (r_pkt_hdr->version != SERVER_PROTOCOL_VERSION) {
                flog::error("Packet has unsupported protocol version (expected {0}, got {1})", SERVER_PROTOCOL_VERSION, r_pkt_hdr->version);
                stopWaiters();
                break;
            }

            if (r_pkt_hdr->size < sizeof(PacketHeader)) {
                flog::error("Packet appears truncated (expected [{0},{1}], got {2})", sizeof(PacketHeader) + 1, SERVER_MAX_PACKET_SIZE, r_pkt_hdr->size);
                stopWaiters();
                break;
            }

            if (r_pkt_hdr->size == sizeof(PacketHeader)) {
                flog::error("Packet has no payload");
                stopWaiters();
                break;
            }

            if (r_pkt_hdr->size > SERVER_MAX_PACKET_SIZE) {
                flog::error("Packet is too large (expected [{0},{1}], got {2})", sizeof(PacketHeader) + 1, SERVER_MAX_PACKET_SIZE, r_pkt_hdr->size);
                stopWaiters();
                break;
            }

            if (useEncryption && !(r_pkt_hdr->flags & PACKET_FLAGS_ENCRYPTED)) {
                flog::error("Got unencrypted packet while encryption is required");
                stopWaiters();
                break;
            }

            if (r_pkt_hdr->seqNr <= lastRecvSeqNr) {
                flog::error("Replay detected: expected seq >=#{0}, got #{1}", lastRecvSeqNr + 1, r_pkt_hdr->seqNr);
                stopWaiters();
                break;
            }

            lastRecvSeqNr = r_pkt_hdr->seqNr;

            // Receive remaining data
            if (sock->recv(&rbuffer[sizeof(PacketHeader)], r_pkt_hdr->size - sizeof(PacketHeader), true, PROTOCOL_TIMEOUT_MS) <= 0) {
                stopWaiters();
                break;
            }

            // Increment data counter
            bytes += r_pkt_hdr->size;

            int payloadLen = r_pkt_hdr->size - sizeof(PacketHeader);

            // Decrypt
            if (useEncryption && (r_pkt_hdr->flags & PACKET_FLAGS_ENCRYPTED)) {
                const CryptoResult_e crres = Crypto_Decrypt(&recvCryptoCtx, netKey, r_pkt_hdr->nonce, r_pkt_hdr->mac, (uint8_t*)r_pkt_hdr, SERVER_CRYPT_HEADER_AAD_SIZE, r_pkt_data, payloadLen);
                if (crres != CRYPTO_OK) {
                    flog::error("Failed to decrypt {0} packet from server; error code: {1}", packetTypeToString(r_pkt_hdr->type), (int)crres);
                    stopWaiters();
                    break;
                }
            }

            // Decode packet
            if (r_pkt_hdr->type == PACKET_TYPE_COMMAND) {
                // TODO: Move to command handler
                if (r_cmd_hdr->cmd == COMMAND_SET_SAMPLE_RATE && payloadLen == sizeof(CommandHeader) + sizeof(double)) {
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
                    const size_t maxSize = STREAM_BUFFER_SIZE * sizeof(dsp::complex_t) + 8;
                    size_t outCount = ZSTD_decompressDCtx(dctx, decompIn.writeBuf, maxSize, r_pkt_data, payloadLen);
                    if (ZSTD_isError(outCount)) {
                        flog::error("Error decompressing baseband packet: {0}", ZSTD_getErrorName(outCount));
                        stopWaiters();
                        break;
                    }
                    else if (outCount > maxSize) {
                        flog::error("Buffer overflow while decompressing baseband packet: {0} > {1}", outCount, maxSize);
                        stopWaiters();
                        break;
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
                stopWaiters();
                break;
            }
        }
    }

    void Client::resetCryptoCounters() {
        sendCryptoCtx.counter = 0;
        nextSendSeqNr = 1;
        recvCryptoCtx.counter = 0;
        lastRecvSeqNr = 0;
    }

    int Client::getUI() {
        if (!isOpen()) { return -1; }
        auto waiter = awaitCommandAck(COMMAND_GET_UI);
        if (!sendCommand(COMMAND_GET_UI, 0)) {
            flog::error("Packet send failure");
            waiter->handled();
            return CONN_ERR_SEND_FAIL;
        }
        if (waiter->await(PROTOCOL_TIMEOUT_MS)) {
            std::lock_guard lck(dlMtx);

            if (r_pkt_hdr->size > SERVER_MAX_PACKET_SIZE) {
                flog::error("Packet is too large; {0} > {1}", r_pkt_hdr->size, SERVER_MAX_PACKET_SIZE);
                waiter->handled();
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

    bool Client::sendPacket(PacketType type, int len) {
        s_pkt_hdr->magic = SERVER_PACKET_WIRE_MAGIC;
        s_pkt_hdr->version = SERVER_PROTOCOL_VERSION;
        s_pkt_hdr->type = type;
        s_pkt_hdr->size = sizeof(PacketHeader) + len;
        s_pkt_hdr->flags = 0;
        s_pkt_hdr->timeUs = getTimeUs();
        s_pkt_hdr->seqNr = nextSendSeqNr++;

        if (useEncryption) {
            s_pkt_hdr->flags |= PACKET_FLAGS_ENCRYPTED;

            Crypto_GenerateRandom(s_pkt_hdr->nonce, CHACHA20_IV_LEN);
            const CryptoResult_e crres = Crypto_Encrypt(&sendCryptoCtx, netKey, s_pkt_hdr->nonce, s_pkt_hdr->mac, (uint8_t*)s_pkt_hdr, SERVER_CRYPT_HEADER_AAD_SIZE, s_pkt_data, len);

            if (crres != CRYPTO_OK) {
                flog::error("Failed to encrypt {0} packet for server; error code: {1}", packetTypeToString(s_pkt_hdr->type), (int)crres);
                return false;
            }
        }
        else {
            memset(s_pkt_hdr->nonce, 0, sizeof(s_pkt_hdr->nonce));
            memset(s_pkt_hdr->mac, 0, sizeof(s_pkt_hdr->mac));
        }

        return sock->send(sbuffer, s_pkt_hdr->size) == s_pkt_hdr->size;
    }

    bool Client::sendCommand(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        return sendPacket(PACKET_TYPE_COMMAND, sizeof(CommandHeader) + len);
    }

    bool Client::sendCommandAck(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        return sendPacket(PACKET_TYPE_COMMAND_ACK, sizeof(CommandHeader) + len);
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