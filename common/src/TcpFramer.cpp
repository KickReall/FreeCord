#include "TcpFramer.h"

namespace {

#ifdef _WIN32
    constexpr int kSendFlags = 0;
#else
    // Без этого флага разрыв соединения посреди send() убивает процесс сигналом SIGPIPE —
    // на Windows такого сигнала не существует, поэтому там он не нужен.
    constexpr int kSendFlags = MSG_NOSIGNAL;
#endif

    // Гарантированно отправляет ровно `size` байт — send() может отправить меньше за один вызов.
    bool SendAll(socket_t socket, const uint8_t* data, size_t size) {
        size_t totalSent = 0;
        while (totalSent < size) {
            int sent = send(socket, reinterpret_cast<const char*>(data + totalSent),
                static_cast<int>(size - totalSent), kSendFlags);
            if (sent == -1) {
                return false;
            }
            totalSent += static_cast<size_t>(sent);
        }
        return true;
    }

    // Гарантированно читает ровно `size` байт.
    // Timeout возвращается ТОЛЬКО если не успели прочитать ни одного байта —
    // иначе кадр пришёл частично, и бросать его на полпути нельзя.
    FrameResult RecvAll(socket_t socket, uint8_t* data, size_t size) {
        size_t totalReceived = 0;
        while (totalReceived < size) {
            int received = recv(socket, reinterpret_cast<char*>(data + totalReceived),
                static_cast<int>(size - totalReceived), 0);
            if (received == 0) {
                return FrameResult::ConnectionClosed;
            }
            if (received == -1) {
                int error = GetLastSocketError();
                if (IsTimeoutError(error)) {
                    // Тишина в начале кадра — нормальная ситуация, сообщаем наверх.
                    // Тишина в середине кадра — продолжаем ждать остаток.
                    if (totalReceived == 0) return FrameResult::Timeout;
                    continue;
                }
                return FrameResult::Error;
            }
            totalReceived += static_cast<size_t>(received);
        }
        return FrameResult::Ok;
    }

} // namespace

FrameResult SendFrame(socket_t socket, uint16_t messageType, uint32_t sequence, const std::vector<uint8_t>& payload) {
    ControlHeader header{};
    header.length = static_cast<uint32_t>(payload.size());
    header.messageType = messageType;
    header.sequence = sequence;

    if (!SendAll(socket, reinterpret_cast<const uint8_t*>(&header), sizeof(header))) {
        return FrameResult::Error;
    }
    if (!payload.empty() && !SendAll(socket, payload.data(), payload.size())) {
        return FrameResult::Error;
    }
    return FrameResult::Ok;
}

FrameResult ReceiveFrame(socket_t socket, Frame& outFrame) {
    ControlHeader header{};
    FrameResult headerResult = RecvAll(socket, reinterpret_cast<uint8_t*>(&header), sizeof(header));
    if (headerResult != FrameResult::Ok) {
        return headerResult;
    }

    // Защита от мусорных/повреждённых заголовков — не пытаемся выделить гигабайты под payload.
    constexpr uint32_t kMaxPayloadSize = 16 * 1024 * 1024; // 16 МБ
    if (header.length > kMaxPayloadSize) {
        return FrameResult::Error;
    }

    outFrame.messageType = header.messageType;
    outFrame.sequence = header.sequence;
    outFrame.payload.resize(header.length);

    if (header.length > 0) {
        FrameResult payloadResult = RecvAll(socket, outFrame.payload.data(), header.length);
        if (payloadResult != FrameResult::Ok) {
            return payloadResult;
        }
    }
    return FrameResult::Ok;
}