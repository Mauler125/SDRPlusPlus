#pragma once
#include <stdint.h>
#include <gui/smgui.h>
#include <dsp/types.h>
#include <utils/chacha20.h>
#include <utils/poly1305.h>

#define SERVER_PACKET_WIRE_MAGIC ('S' | ('D' << 8) | ('R' << 16))
#define SERVER_PROTOCOL_VERSION 2

#define SERVER_MAX_DATA_SIZE  (STREAM_BUFFER_SIZE * sizeof(dsp::complex_t) * 2)
#define SERVER_DEFAULT_NET_KEY "/+Ies6v6oi4FrWItKopQ6yoHvPhqjiuaLFeZLdLvmCE="

namespace server {
    enum PacketType {
        // Client to Server
        PACKET_TYPE_COMMAND,
        PACKET_TYPE_COMMAND_ACK,
        PACKET_TYPE_BASEBAND,
        PACKET_TYPE_VFO,
        PACKET_TYPE_FFT,
        PACKET_TYPE_ERROR
    };

    inline const char* packetTypeToString(const uint32_t type) {
        switch (type) {
        case PACKET_TYPE_COMMAND:
            return "command";
        case PACKET_TYPE_COMMAND_ACK:
            return "command_ack";
        case PACKET_TYPE_BASEBAND:
            return "baseband";
        case PACKET_TYPE_VFO:
            return "vfo";
        case PACKET_TYPE_FFT:
            return "fft";
        case PACKET_TYPE_ERROR:
            return "error";
        default:
            return "unknown_type";
        }
    }

    enum PacketFlags {
        PACKET_FLAGS_COMPRESSED = 1 << 0,
        PACKET_FLAGS_ENCRYPTED = 1 << 1
    };

    enum Command {
        // Client to Server
        COMMAND_GET_UI = 0x00,
        COMMAND_UI_ACTION,
        COMMAND_START,
        COMMAND_STOP,
        COMMAND_SET_FREQUENCY,
        COMMAND_GET_SAMPLE_RATE,
        COMMAND_SET_SAMPLE_TYPE,
        COMMAND_SET_COMPRESSION,

        // Server to client
        COMMAND_SET_UI = 0x80,
        COMMAND_SET_SAMPLE_RATE,
        COMMAND_DISCONNECT
    };

    inline const char* commandTypeToString(const uint32_t type) {
        switch (type) {
        case COMMAND_GET_UI:
            return "get_ui";
        case COMMAND_SET_UI:
            return "set_ui";
        case COMMAND_UI_ACTION:
            return "ui_action";
        case COMMAND_START:
            return "start";
        case COMMAND_STOP:
            return "stop";
        case COMMAND_SET_FREQUENCY:
            return "set_frequency";
        case COMMAND_GET_SAMPLE_RATE:
            return "get_sample_rate";
        case COMMAND_SET_SAMPLE_TYPE:
            return "set_sample_type";
        case COMMAND_SET_COMPRESSION:
            return "set_compression";
        case COMMAND_SET_SAMPLE_RATE:
            return "set_sample_rate";
        case COMMAND_DISCONNECT:
            return "disconnect";
        default:
            return "unknown_type";
        }
    }

    enum Error {
        ERROR_NONE = 0x00,
        ERROR_INVALID_PACKET,
        ERROR_INVALID_COMMAND,
        ERROR_INVALID_ARGUMENT
    };
    
#pragma pack(push, 1)
    struct PacketHeader {
        uint32_t magic : 24;
        uint32_t version : 8;
        uint32_t type;
        uint32_t size;
        uint32_t flags;
        uint64_t timeUs;
        uint64_t seqNr;
        ChaCha20_Nonce96_t nonce;
        Poly1305_Tag_t mac; // must be the last since it will be excluded from tag creation!
    };

    struct CommandHeader {
        uint32_t cmd;
    };
#pragma pack(pop)
}

#define SERVER_HEADER_DATA_SIZE      (sizeof(server::PacketHeader) + sizeof(server::CommandHeader))
#define SERVER_MAX_PACKET_SIZE       (SERVER_HEADER_DATA_SIZE + SERVER_MAX_DATA_SIZE)
#define SERVER_CRYPT_HEADER_AAD_SIZE (sizeof(server::PacketHeader) - sizeof(Poly1305_Tag_t))
