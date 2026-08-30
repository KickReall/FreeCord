#pragma once
#include <vector>
#include <cstdint>
#include "PlatformSocket.h"
#include "Transport.h"
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

// Отправляет/читает один кадр целиком через произвольный транспорт (обычный
// сокет или TLS). Блокирующие вызовы.
FrameResult SendFrame(ITransport& transport, uint16_t messageType, uint32_t sequence, const std::vector<uint8_t>& payload);
FrameResult ReceiveFrame(ITransport& transport, Frame& outFrame);

// Удобные перегрузки поверх голого сокета — для внутренних вызовов между
// сервисами, где TLS не используется. Оборачивают сокет в PlainTransport и
// вызывают версии выше.
inline FrameResult SendFrame(socket_t socket, uint16_t messageType, uint32_t sequence, const std::vector<uint8_t>& payload) {
    PlainTransport transport(socket);
    return SendFrame(transport, messageType, sequence, payload);
}

inline FrameResult ReceiveFrame(socket_t socket, Frame& outFrame) {
    PlainTransport transport(socket);
    return ReceiveFrame(transport, outFrame);
}
