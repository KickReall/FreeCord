#pragma once
#include <string>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "TcpFramer.h"
#include "ProtocolTypes.h"

// Одноразовый запрос к внутреннему сервису: connect -> send -> recv -> close.
// Сервисы закрывают соединение после каждого кадра, поэтому переиспользовать сокет нельзя.
inline bool CallService(const char* host, int port,
    MessageType requestType, const std::vector<uint8_t>& payload,
    MessageType expectedResponseType, Frame& outResponse) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    if (SendFrame(sock, static_cast<uint16_t>(requestType), 1, payload) != FrameResult::Ok) {
        closesocket(sock);
        return false;
    }

    FrameResult result = ReceiveFrame(sock, outResponse);
    closesocket(sock);

    if (result != FrameResult::Ok) return false;
    return outResponse.messageType == static_cast<uint16_t>(expectedResponseType);
}