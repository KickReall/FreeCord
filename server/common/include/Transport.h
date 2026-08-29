#pragma once
#include <cstdint>
#include "PlatformSocket.h"

// Абстракция поверх способа передачи байт, чтобы TcpFramer не знал, шифрован
// канал (TlsTransport) или нет (PlainTransport). Семантика методов повторяет
// send()/recv(): Send/Recv возвращают -1 на ошибке, Recv возвращает 0 при
// закрытии соединения, LastRecvTimedOut() говорит, была ли последняя ошибка
// таймаутом чтения (а не разрывом).
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual int Send(const uint8_t* data, int size) = 0;
    virtual int Recv(uint8_t* data, int size) = 0;
    virtual bool LastRecvTimedOut() = 0;
};

// Нешифрованный TCP-сокет — то же поведение, что было в TcpFramer до появления TLS.
class PlainTransport : public ITransport {
public:
    explicit PlainTransport(socket_t socket) : m_socket(socket) {}

    int Send(const uint8_t* data, int size) override;
    int Recv(uint8_t* data, int size) override;
    bool LastRecvTimedOut() override;

private:
    socket_t m_socket;
    bool m_lastRecvTimedOut = false;
};
