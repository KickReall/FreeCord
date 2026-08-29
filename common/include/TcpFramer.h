#pragma once
#include <vector>
#include <cstdint>
#include "PlatformSocket.h"
#include "ProtocolTypes.h"

enum class FrameResult {
    Ok,
    Timeout,            // за отведённое время ничего не пришло — не ошибка, просто тишина
    ConnectionClosed,
    Error
};

struct Frame {
    uint16_t messageType = 0;
    uint32_t sequence = 0;
    std::vector<uint8_t> payload;
};

// Отправляет один кадр (заголовок + payload) целиком. Блокирующий вызов.
FrameResult SendFrame(socket_t socket, uint16_t messageType, uint32_t sequence, const std::vector<uint8_t>& payload);

// Читает один кадр целиком. Блокирует, пока не придёт весь кадр, либо не оборвётся соединение/ошибка.
FrameResult ReceiveFrame(socket_t socket, Frame& outFrame);