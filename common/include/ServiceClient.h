#pragma once
#include <string>
#include <vector>
#include "PlatformSocket.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"

// Одноразовый запрос к внутреннему сервису: connect -> send -> recv -> close.
// Сервисы закрывают соединение после каждого кадра, поэтому переиспользовать сокет нельзя.
inline bool CallService(const char* host, int port,
    MessageType requestType, const std::vector<uint8_t>& payload,
    MessageType expectedResponseType, Frame& outResponse) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == kInvalidSocket) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        CloseSocket(sock);
        return false;
    }

    if (SendFrame(sock, static_cast<uint16_t>(requestType), 1, payload) != FrameResult::Ok) {
        CloseSocket(sock);
        return false;
    }

    FrameResult result = ReceiveFrame(sock, outResponse);
    CloseSocket(sock);

    if (result != FrameResult::Ok) return false;
    return outResponse.messageType == static_cast<uint16_t>(expectedResponseType);
}