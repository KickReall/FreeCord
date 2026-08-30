#include "Transport.h"

int PlainTransport::Send(const uint8_t* data, int size) {
#ifdef _WIN32
    constexpr int kSendFlags = 0;
#else
    // Без этого флага разрыв соединения посреди send() убивает процесс SIGPIPE.
    constexpr int kSendFlags = MSG_NOSIGNAL;
#endif
    return send(m_socket, reinterpret_cast<const char*>(data), size, kSendFlags);
}

int PlainTransport::Recv(uint8_t* data, int size) {
    int received = recv(m_socket, reinterpret_cast<char*>(data), size, 0);
    if (received == -1) {
        m_lastRecvTimedOut = IsTimeoutError(GetLastSocketError());
    }
    else {
        m_lastRecvTimedOut = false;
    }
    return received;
}

bool PlainTransport::LastRecvTimedOut() {
    return m_lastRecvTimedOut;
}
