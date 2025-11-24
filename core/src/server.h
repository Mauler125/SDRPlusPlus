#pragma once
#include <utils/networking.h>
#include <dsp/stream.h>
#include <dsp/types.h>
#include <server_protocol.h>

namespace server {
    void setInput(dsp::stream<dsp::complex_t>* stream);
    int main();

    void _clientHandler(net::Conn conn, void* ctx);
    void _packetHandler(int count, uint8_t* buf, void* ctx);
    void _disconnectHandler(int err, void* ctx);
    void _testServerHandler(uint8_t* data, int count, void* ctx);

    void drawMenu();

    void commandHandler(Command cmd, uint8_t* data, int len);
    void renderUI(SmGui::DrawList* dl, std::string diffId, SmGui::DrawListElem diffValue);
    bool sendUI(Command originCmd, std::string diffId, SmGui::DrawListElem diffValue);
    bool sendError(Error err);
    bool sendSampleRate(double sampleRate);
    bool setInputSampleRate(double samplerate);

    bool sendPacket(PacketType type, int len);
    bool sendCommand(Command cmd, int len);
    bool sendCommandAck(Command cmd, int len);
}
