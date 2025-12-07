#pragma once
#include <stdint.h>
#include <gui/smgui.h>
#include <dsp/types.h>
#include <utils/chacha20.h>

#define SERVER_PACKET_WIRE_MAGIC ('S' | ('D' << 8) | ('R' << 16))
#define SERVER_PROTOCOL_VERSION 1

#define SERVER_MAX_PACKET_SIZE  (STREAM_BUFFER_SIZE * sizeof(dsp::complex_t) * 2)
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
        COMMAND_GET_SAMPLERATE,
        COMMAND_SET_SAMPLE_TYPE,
        COMMAND_SET_COMPRESSION,

        // Server to client
        COMMAND_SET_SAMPLERATE = 0x80,
        COMMAND_DISCONNECT
    };

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
        uint32_t checksum;
        uint8_t nonce[CHACHA20_IV_LEN];
    };

    struct CommandHeader {
        uint32_t cmd;
    };
#pragma pack(pop)
}