#include "server.h"
#include "core.h"
#include <utils/flog.h>
#include <version.h>
#include <config.h>
#include <filesystem>
#include <csignal>
#include <dsp/types.h>
#include <signal_path/signal_path.h>
#include <gui/smgui.h>
#include <utils/optionlist.h>
#include "dsp/compression/sample_stream_compressor.h"
#include "dsp/sink/handler_sink.h"
#include <zstd.h>
#include <utils/chacha20.h>
#include <utils/crc32.h>
#include <utils/str_tools.h>
#include <utils/crypto.h>
#include <dsp/math/bitop.h>

namespace server {
    dsp::stream<dsp::complex_t> dummyInput;
    dsp::compression::SampleStreamCompressor comp;
    dsp::sink::Handler<uint8_t> hnd;
    net::Conn client;
    std::recursive_mutex clientMtx;
    uint8_t* rbuf = NULL;
    uint8_t* sbuf = NULL;
    uint8_t* bbuf = NULL;

    PacketHeader* r_pkt_hdr = NULL;
    uint8_t* r_pkt_data = NULL;
    CommandHeader* r_cmd_hdr = NULL;
    uint8_t* r_cmd_data = NULL;

    PacketHeader* s_pkt_hdr = NULL;
    uint8_t* s_pkt_data = NULL;
    CommandHeader* s_cmd_hdr = NULL;
    uint8_t* s_cmd_data = NULL;

    PacketHeader* bb_pkt_hdr = NULL;
    uint8_t* bb_pkt_data = NULL;

    SmGui::DrawListElem dummyElem;

    ZSTD_CCtx* cctx = NULL;

    net::Listener listener;

    OptionList<std::string, std::string> sourceList;
    int sourceId = 0;
    bool running = false;
    bool canUseCompression = false;
    bool compression = true;
    bool useEncryption = false;
    bool doShutdown = false;
    uint8_t netKey[CHACHA20_KEY_LEN];

    double sampleRate = 1000000.0;

    static void initCompressor() {
        cctx = ZSTD_createCCtx();
        canUseCompression = cctx != NULL;

        if (!cctx) {
            flog::warn("ZStd failed to initialize; {0}!", "compression will be disabled");
        }
    }

    static void shutdownCompressor() {
        const size_t ret = ZSTD_freeCCtx(cctx);
        if (ZSTD_isError(ret)) {
            flog::error("ZStd failed to shutdown; {0}!", ZSTD_getErrorName(ret));
        }
        cctx = NULL;
        canUseCompression = false;
    }

    static void initDSP() {
        // Initialize DSP
        comp.init(&dummyInput, dsp::compression::PCM_TYPE_I8);
        hnd.init(&comp.out, _testServerHandler, NULL);
        rbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        sbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        bbuf = new uint8_t[SERVER_MAX_PACKET_SIZE];
        comp.start();
        hnd.start();

        // Initialize headers
        r_pkt_hdr = (PacketHeader*)rbuf;
        r_pkt_data = &rbuf[sizeof(PacketHeader)];
        r_cmd_hdr = (CommandHeader*)r_pkt_data;
        r_cmd_data = &rbuf[sizeof(PacketHeader) + sizeof(CommandHeader)];

        s_pkt_hdr = (PacketHeader*)sbuf;
        s_pkt_data = &sbuf[sizeof(PacketHeader)];
        s_cmd_hdr = (CommandHeader*)s_pkt_data;
        s_cmd_data = &sbuf[sizeof(PacketHeader) + sizeof(CommandHeader)];

        bb_pkt_hdr = (PacketHeader*)bbuf;
        bb_pkt_data = &bbuf[sizeof(PacketHeader)];
    }

    static void shutdownDSP() {
        // Shutdown DSP
        hnd.stop();
        comp.stop();
        delete[] bbuf;
        delete[] sbuf;
        delete[] rbuf;
        hnd.shutdown();
        comp.shutdown();

        // Shutdown headers
        bb_pkt_hdr = NULL;
        bb_pkt_data = NULL;

        s_pkt_hdr = NULL;
        s_pkt_data = NULL;
        s_cmd_hdr = NULL;
        s_cmd_data = NULL;

        r_pkt_hdr = NULL;
        r_pkt_data = NULL;
        r_cmd_hdr = NULL;
        r_cmd_data = NULL;
    }

    static void initCrypto() {
        const std::string& keyB64 = core::args["key"];

        if (keyB64.empty()) {
            return; // Disabled.
        }

        std::string keyB64_Trimmed;

        if (!utils::isValidBase64(keyB64, &keyB64_Trimmed)) {
            flog::error("Net key {0} is not a valid base64 string; encryption will be disabled!", keyB64);
            return;
        }

        std::string decodedKey;

        if (!utils::decodeBase64(keyB64_Trimmed, decodedKey)) {
            flog::error("Net key {0} failed to decode; encryption will be disabled!", keyB64);
            return;
        }

        if (decodedKey.length() != CHACHA20_KEY_LEN) {
            flog::error("Net key {0} decoded to size {1}, but {2} was expected; encryption will be disabled!", keyB64, decodedKey.length(), CHACHA20_KEY_LEN);
            return;
        }

        memcpy(netKey, decodedKey.data(), CHACHA20_KEY_LEN);
        useEncryption = true;
    }

    static void shutdownCrypto() {
        memset(&netKey, 0, CHACHA20_KEY_LEN);
        useEncryption = false;
    }

    static void signalHandler(int signum) {
        flog::info("Received signal #{0}, shutting down...", signum);
        doShutdown = true;
    }

    int main() {
        flog::info("=====| SERVER MODE |=====");

        if (Crypto_InitRandom() != 0) {
            return 1;
        }

        std::signal(SIGINT, signalHandler);  // Ctrl+C
        std::signal(SIGTERM, signalHandler); // Termination

        initDSP();
        initCompressor();

        // Load config
        core::configManager.acquire();
        std::string modulesDir = core::configManager.conf["modulesDirectory"];
        std::vector<std::string> modules = core::configManager.conf["modules"];
        auto modList = core::configManager.conf["moduleInstances"].items();
        std::string sourceName = core::configManager.conf["source"];
        core::configManager.release();

        initCrypto();
        modulesDir = std::filesystem::absolute(modulesDir).string();

        // Initialize SmGui in server mode
        SmGui::init(true);

        flog::info("Loading modules");
        // Load modules and check type to only load sources ( TODO: Have a proper type parameter int the info )
        // TODO LATER: Add whitelist/blacklist stuff
        if (std::filesystem::is_directory(modulesDir)) {
            for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
                std::string path = file.path().generic_string();
                std::string fn = file.path().filename().string();
                if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                    continue;
                }
                if (!file.is_regular_file()) { continue; }
                if (fn.find("source") == std::string::npos) { continue; }

                flog::info("Loading {0}", path);
                core::moduleManager.loadModule(path);
            }
        }
        else {
            flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
        }

        // Load additional modules through the config ( TODO: Have a proper type parameter int the info )
        // TODO LATER: Add whitelist/blacklist stuff
        for (auto const& apath : modules) {
            std::filesystem::path file = std::filesystem::absolute(apath);
            std::string path = file.generic_string();
            std::string fn = file.filename().string();
            if (file.extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!std::filesystem::is_regular_file(file)) { continue; }
            if (fn.find("source") == std::string::npos) { continue; }

            flog::info("Loading {0}", path);
            core::moduleManager.loadModule(path);
        }

        // Create module instances
        for (auto const& [name, _module] : modList) {
            std::string mod = _module["module"];
            bool enabled = _module["enabled"];
            if (core::moduleManager.modules.find(mod) == core::moduleManager.modules.end()) { continue; }
            flog::info("Initializing {0} ({1})", name, mod);
            core::moduleManager.createInstance(name, mod);
            if (!enabled) { core::moduleManager.disableInstance(name); }
        }

        // Do post-init
        core::moduleManager.doPostInitAll();

        // Generate source list
        auto list = sigpath::sourceManager.getSourceNames();
        for (auto& name : list) {
            sourceList.define(name, name);
        }

        // Load sourceId from config
        sourceId = 0;
        if (sourceList.keyExists(sourceName)) {
            sourceId = sourceList.keyId(sourceName);
        }

        if (!sourceList.empty()) {
            sigpath::sourceManager.selectSource(sourceList[sourceId]);
        }
        else {
            flog::warn("Source list is empty!");
        }

        // TODO: Use command line option
        const std::string& host = core::args["address"];
        const int port = (int)core::args["port"];
        listener = net::listen(host, port);
        listener->acceptAsync(_clientHandler, NULL);

        std::string keyStr;
        if (useEncryption) {
            utils::encodeBase64(std::string_view((char*)netKey, std::size(netKey)), keyStr);
        }

        flog::info("Ready, listening on [{0}]:{1} with key '{2}'", host, port, keyStr.c_str());
        while (!doShutdown) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }

        shutdownCrypto();
        shutdownCompressor();
        shutdownDSP();
        Crypto_ShutdownRandom();

        return 0;
    }

    void _disconnectHandler(int err, void* ctx) {
        std::lock_guard<std::recursive_mutex> lk(clientMtx);
        if (client) {
            char ipStr[INET6_ADDRSTRLEN];
            const size_t ipStrSz = client->toString(ipStr, sizeof(ipStr));
            const std::string_view ipStrView(ipStr, ipStrSz);
            flog::info("Client {0} disconnected with code {1}.", ipStrView, err);

            client->setDisconnectFlag();
        }
    }

    static uint64_t getTimeUs() {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void _clientHandler(net::Conn conn, void* ctx) {
        char ipStr[INET6_ADDRSTRLEN];
        const size_t ipStrSz = conn->toString(ipStr, sizeof(ipStr));
        const std::string_view ipStrView(ipStr, ipStrSz);

        // Re-arm it to process new incoming connects
        if (listener) { listener->acceptAsync(_clientHandler, NULL); }

        // Reject if someone else is already connected
        {
            std::lock_guard<std::recursive_mutex> lk(clientMtx);
            if (client && client->isOpen()) {
                flog::info("Rejected connection from {0}; another client is already connected", ipStrView);

                // Issue a disconnect command to the client
                uint8_t buf[sizeof(PacketHeader) + sizeof(CommandHeader)];
                PacketHeader* tmp_phdr = (PacketHeader*)buf;
                CommandHeader* tmp_chdr = (CommandHeader*)&buf[sizeof(PacketHeader)];
                tmp_phdr->magic = SERVER_PACKET_WIRE_MAGIC;
                tmp_phdr->version = SERVER_PROTOCOL_VERSION;
                tmp_phdr->size = sizeof(PacketHeader) + sizeof(CommandHeader);
                tmp_phdr->type = PACKET_TYPE_COMMAND;
                tmp_phdr->flags = 0; // No compression on initial reject
                tmp_phdr->seqNr = conn->nextSendSeqNr++;
                tmp_phdr->timeUs = getTimeUs();
                tmp_chdr->cmd = COMMAND_DISCONNECT;

                if (useEncryption) {
                    tmp_phdr->flags |= PACKET_FLAGS_ENCRYPTED;

                    Crypto_GenerateRandom(tmp_phdr->nonce, CHACHA20_IV_LEN);
                    const CryptoResult_e crres = Crypto_Encrypt(&conn->sendCryptoCtx, netKey, tmp_phdr->nonce, tmp_phdr->mac, (uint8_t*)tmp_phdr, SERVER_CRYPT_HEADER_AAD_SIZE, &buf[sizeof(PacketHeader)], sizeof(CommandHeader));

                    if (crres != CRYPTO_OK) {
                        tmp_phdr->flags &= ~PACKET_FLAGS_ENCRYPTED;
                    }
                }
                else {
                    memset(tmp_phdr->nonce, 0, sizeof(tmp_phdr->nonce));
                    memset(tmp_phdr->mac, 0, sizeof(tmp_phdr->mac));
                }

                conn->write(tmp_phdr->size, buf);
                conn->close();

                // TODO: Find something cleaner
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return;
            }
        }

        {
            std::lock_guard<std::recursive_mutex> lk(clientMtx);
            client = std::move(conn);
            flog::info("Accepted connection from {0}", ipStrView);
            client->readAsync(sizeof(PacketHeader), rbuf, _packetHandler, _disconnectHandler, NULL);
        }

        // Perform settings reset
        sigpath::sourceManager.stop();
        comp.setPCMType(dsp::compression::PCM_TYPE_I16);
        compression = false;
    }

    void _packetHandler(int count, uint8_t* buf, void* ctx) {
        assert(count > 0); // code bug in readAsync
        PacketHeader* hdr = (PacketHeader*)buf;

        if (hdr->magic != SERVER_PACKET_WIRE_MAGIC) {
            flog::error("Packet has bad magic (expected {0}, got {1})", SERVER_PACKET_WIRE_MAGIC, hdr->magic);
            _disconnectHandler(-1, ctx);
            return;
        }

        if (hdr->version != SERVER_PROTOCOL_VERSION) {
            flog::error("Packet has unsupported protocol version (expected {0}, got {1})", SERVER_PROTOCOL_VERSION, hdr->version);
            _disconnectHandler(-1, ctx);
            return;
        }

        if (hdr->size < sizeof(PacketHeader)) {
            flog::error("Packet appears truncated (expected [{0},{1}], got {2})", sizeof(PacketHeader)+1, SERVER_MAX_PACKET_SIZE, hdr->size);
            _disconnectHandler(-1, ctx);
            return;
        }

        if (hdr->size == sizeof(PacketHeader)) {
            flog::error("Packet has no payload");
            _disconnectHandler(-1, ctx);
            return;
        }

        if (hdr->size > SERVER_MAX_PACKET_SIZE) {
            flog::error("Packet is too large (expected [{0},{1}], got {2})", sizeof(PacketHeader)+1, SERVER_MAX_PACKET_SIZE, hdr->size);
            _disconnectHandler(-1, ctx);
            return;
        }

        if (useEncryption && !(hdr->flags & PACKET_FLAGS_ENCRYPTED)) {
            flog::error("Got unencrypted packet while encryption is required");
            _disconnectHandler(-1, ctx);
            return;
        }

        std::lock_guard<std::recursive_mutex> lk(clientMtx);

        if (hdr->seqNr <= client->lastRecvSeqNr) {
            flog::error("Replay detected: expected seq >=#{0}, got #{1}", client->lastRecvSeqNr + 1, hdr->seqNr);
            _disconnectHandler(-1, ctx);
            return;
        }

        client->lastRecvSeqNr = hdr->seqNr;

        // Read the rest of the data
        int len = 0;
        int read = 0;
        int goal = hdr->size - sizeof(PacketHeader);
        while (len < goal) {
            read = client->read(goal - len, &buf[sizeof(PacketHeader) + len]);
            if (read <= 0) {
                // Client disconnected mid-packet.
                _disconnectHandler(read, ctx);
                return;
            }
            len += read;
        }

        const int payloadLen = hdr->size - sizeof(PacketHeader);

        if (useEncryption && (hdr->flags & PACKET_FLAGS_ENCRYPTED)) {
            const CryptoResult_e crres = Crypto_Decrypt(&client->recvCryptoCtx, netKey, hdr->nonce, hdr->mac, (uint8_t*)hdr, SERVER_CRYPT_HEADER_AAD_SIZE, &buf[sizeof(PacketHeader)], payloadLen);

            if (crres != CRYPTO_OK) {
                flog::error("Failed to decrypt {0} packet from client; error code: {1}", packetTypeToString(hdr->type), (int)crres);
                _disconnectHandler(-1, ctx);

                return;
            }
        }

        // Parse and process
        if (hdr->type == PACKET_TYPE_COMMAND && payloadLen >= sizeof(CommandHeader)) {
            CommandHeader* chdr = (CommandHeader*)&buf[sizeof(PacketHeader)];
            if (!commandHandler((Command)chdr->cmd, &buf[sizeof(PacketHeader) + sizeof(CommandHeader)], payloadLen - sizeof(CommandHeader))) {
                flog::error("Failed to handle command {0} from client", commandTypeToString(chdr->cmd));
                _disconnectHandler(-1, ctx);

                return;
            }
        }
        else {
            sendError(ERROR_INVALID_PACKET);
        }

        // Start another async read if the client is still connected
        if (client && client->isOpen()) {
            client->readAsync(sizeof(PacketHeader), rbuf, _packetHandler, _disconnectHandler, NULL);
        }
    }

    void _testServerHandler(uint8_t* data, int count, void* ctx) {
        std::lock_guard<std::recursive_mutex> lk(clientMtx);
        if (!client || !client->isOpen()) {
            return;
        }

        const int maxPayload = SERVER_MAX_PACKET_SIZE - sizeof(PacketHeader);
        if (count > maxPayload) {
            flog::error("Baseband buffer overflow: {0} > {1}; dropping packet...", count, maxPayload);
            return;
        }

        // Initialize header
        bb_pkt_hdr->magic = SERVER_PACKET_WIRE_MAGIC;
        bb_pkt_hdr->version = SERVER_PROTOCOL_VERSION;
        bb_pkt_hdr->type = PACKET_TYPE_BASEBAND;
        bb_pkt_hdr->flags = 0;

        // Compress data if needed and fill out header fields
        bool didCompress = false;
        int payloadSize = 0;

        if (compression) {
            const size_t compRet = ZSTD_compressCCtx(cctx, &bbuf[sizeof(PacketHeader)], maxPayload, data, count, 6);

            if (ZSTD_isError(compRet)) {
                flog::error("Error compressing baseband packet: {0}; sending uncompressed...", ZSTD_getErrorName(compRet));
            }
            else if (compRet == count) {
                flog::error("Baseband packet compression provided no size reduction; sending uncompressed...");
            }
            else if (compRet > count) {
                flog::error("Baseband packet expanded during compression: {0} >= {1}; sending uncompressed...", compRet, count);
            }
            else {
                payloadSize = (int)compRet;
                didCompress = true;

                bb_pkt_hdr->flags |= PACKET_FLAGS_COMPRESSED;
            }
        }

        if (!didCompress) {
            payloadSize = count;
            memcpy(&bbuf[sizeof(PacketHeader)], data, count);
        }

        bb_pkt_hdr->size = sizeof(PacketHeader) + payloadSize;
        bb_pkt_hdr->timeUs = getTimeUs();
        bb_pkt_hdr->seqNr = client->nextSendSeqNr++;

        if (useEncryption) {
            bb_pkt_hdr->flags |= PACKET_FLAGS_ENCRYPTED;

            Crypto_GenerateRandom(bb_pkt_hdr->nonce, CHACHA20_IV_LEN);
            const CryptoResult_e crres = Crypto_Encrypt(&client->sendCryptoCtx, netKey, bb_pkt_hdr->nonce, bb_pkt_hdr->mac, (uint8_t*)bb_pkt_hdr, SERVER_CRYPT_HEADER_AAD_SIZE, &bbuf[sizeof(PacketHeader)], payloadSize);

            if (crres != CRYPTO_OK) {
                flog::error("Failed to encrypt {0} packet for client; error code: {1}", packetTypeToString(bb_pkt_hdr->type), (int)crres);
                _disconnectHandler(-1, ctx);
                return;
            }
        }
        else {
            memset(bb_pkt_hdr->nonce, 0, sizeof(bb_pkt_hdr->nonce));
            memset(bb_pkt_hdr->mac, 0, sizeof(bb_pkt_hdr->mac));
        }

        // Write to network
        const int lenSend = client->write(bb_pkt_hdr->size, bbuf);

        if (lenSend < bb_pkt_hdr->size) {
            flog::error("Partial baseband payload sent: {0} < {1}", lenSend, bb_pkt_hdr->size);
            _disconnectHandler(-1, ctx);
            return;
        }
    }

    void setInput(dsp::stream<dsp::complex_t>* stream) {
        comp.setInput(stream);
    }

    bool commandHandler(Command cmd, uint8_t* data, int len) {
        if (cmd == COMMAND_GET_UI) {
            return sendUI(COMMAND_GET_UI, "", dummyElem);
        }
        else if (cmd == COMMAND_UI_ACTION && len >= 3) {
            // Check if sending back data is needed
            int i = 0;
            bool sendback = data[i++];
            len--;

            // Load id
            SmGui::DrawListElem diffId;
            int count = SmGui::DrawList::loadItem(diffId, &data[i], len);
            if (count < 0) { return sendError(ERROR_INVALID_ARGUMENT); }
            if (diffId.type != SmGui::DRAW_LIST_ELEM_TYPE_STRING) { return sendError(ERROR_INVALID_ARGUMENT); }
            i += count;
            len -= count;

            // Load value
            SmGui::DrawListElem diffValue;
            count = SmGui::DrawList::loadItem(diffValue, &data[i], len);
            if (count < 0) { return sendError(ERROR_INVALID_ARGUMENT); }
            i += count;
            len -= count;

            // Render and send back
            if (sendback) {
                return sendUI(COMMAND_UI_ACTION, diffId.str, diffValue);
            }
            else {
                renderUI(NULL, diffId.str, diffValue);
            }
        }
        else if (cmd == COMMAND_START) {
            sigpath::sourceManager.start();
            running = true;
        }
        else if (cmd == COMMAND_STOP) {
            sigpath::sourceManager.stop();
            running = false;
        }
        else if (cmd == COMMAND_SET_FREQUENCY && len == 8) {
            sigpath::sourceManager.tune(*(double*)data);
            return sendCommandAck(COMMAND_SET_FREQUENCY, 0);
        }
        else if (cmd == COMMAND_GET_SAMPLE_RATE) {
            return sendSampleRate(sampleRate);
        }
        else if (cmd == COMMAND_SET_SAMPLE_TYPE && len == 1) {
            dsp::compression::PCMType type = (dsp::compression::PCMType)*(uint8_t*)data;
            comp.setPCMType(type);
        }
        else if (cmd == COMMAND_SET_COMPRESSION && len == 1) {
            if (canUseCompression) {
                compression = *(uint8_t*)data;
            }
        }
        else {
            flog::error("Invalid command: {0} (len = {1})", (int)cmd, len);
            return sendError(ERROR_INVALID_COMMAND);
        }

        return true;
    }

    void drawMenu() {
        if (running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo("##sdrpp_server_src_sel", &sourceId, sourceList.txt)) {
            sigpath::sourceManager.selectSource(sourceList[sourceId]);
            core::configManager.acquire();
            core::configManager.conf["source"] = sourceList.key(sourceId);
            core::configManager.release(true);
        }
        if (running) { SmGui::EndDisabled(); }

        sigpath::sourceManager.showSelectedMenu();
    }

    void renderUI(SmGui::DrawList* dl, std::string diffId, SmGui::DrawListElem diffValue) {
        // If we're recording and there's an action, render once with the action and record without

        if (dl && !diffId.empty()) {
            SmGui::setDiff(diffId, diffValue);
            drawMenu();

            SmGui::setDiff("", dummyElem);
            SmGui::startRecord(dl);
            drawMenu();
            SmGui::stopRecord();
        }
        else {
            SmGui::setDiff(diffId, diffValue);
            SmGui::startRecord(dl);
            drawMenu();
            SmGui::stopRecord();
        }
    }

    bool sendUI(Command originCmd, std::string diffId, SmGui::DrawListElem diffValue) {
        // Render UI
        SmGui::DrawList dl;
        renderUI(&dl, diffId, diffValue);

        // Create response
        int size = dl.getSize();
        dl.store(s_cmd_data, size);

        // Send to network
        return sendCommandAck(originCmd, size);
    }

    bool sendError(Error err) {
        PacketHeader* hdr = (PacketHeader*)sbuf;
        s_pkt_data[0] = err;
        return sendPacket(PACKET_TYPE_ERROR, 1);
    }

    bool sendSampleRate(double sampleRate) {
        *(double*)s_cmd_data = sampleRate;
        return sendCommand(COMMAND_SET_SAMPLE_RATE, sizeof(double));
    }

    bool setInputSampleRate(double samplerate) {
        sampleRate = samplerate;
        return sendSampleRate(sampleRate);
    }

    bool sendPacket(PacketType type, int len) {
        std::lock_guard<std::recursive_mutex> lk(clientMtx);

        if (!client || !client->isOpen()) {
            return false;
        }

        const int maxPayload = SERVER_MAX_PACKET_SIZE - sizeof(PacketHeader);
        if (len > maxPayload) {
            flog::error("Send buffer overflow: {0} > {1}; dropping packet...", len, maxPayload);
            return false;
        }

        s_pkt_hdr->magic = SERVER_PACKET_WIRE_MAGIC;
        s_pkt_hdr->version = SERVER_PROTOCOL_VERSION;
        s_pkt_hdr->type = type;
        s_pkt_hdr->size = sizeof(PacketHeader) + len;
        s_pkt_hdr->flags = 0;
        s_pkt_hdr->timeUs = getTimeUs();
        s_pkt_hdr->seqNr = client->nextSendSeqNr++;

        if (useEncryption) {
            s_pkt_hdr->flags |= PACKET_FLAGS_ENCRYPTED;

            Crypto_GenerateRandom(s_pkt_hdr->nonce, CHACHA20_IV_LEN);
            const CryptoResult_e crres = Crypto_Encrypt(&client->sendCryptoCtx, netKey, s_pkt_hdr->nonce, s_pkt_hdr->mac, (uint8_t*)s_pkt_hdr, SERVER_CRYPT_HEADER_AAD_SIZE, &sbuf[sizeof(PacketHeader)], len);

            if (crres != CRYPTO_OK) {
                flog::error("Failed to encrypt {0} packet for client; error code: {1}", packetTypeToString(s_pkt_hdr->type), (int)crres);
                return false;
            }
        }
        else {
            memset(s_pkt_hdr->nonce, 0, sizeof(s_pkt_hdr->nonce));
            memset(s_pkt_hdr->mac, 0, sizeof(s_pkt_hdr->mac));
        }

        return client->write(s_pkt_hdr->size, sbuf) == s_pkt_hdr->size;
    }

    bool sendCommand(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        return sendPacket(PACKET_TYPE_COMMAND, sizeof(CommandHeader) + len);
    }

    bool sendCommandAck(Command cmd, int len) {
        s_cmd_hdr->cmd = cmd;
        return sendPacket(PACKET_TYPE_COMMAND_ACK, sizeof(CommandHeader) + len);
    }
}