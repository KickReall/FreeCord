#include "PlatformSocket.h"

#ifndef _WIN32
    #include <cerrno>
#endif

void CloseSocket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

int GetLastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool IsTimeoutError(int err) {
#ifdef _WIN32
    return err == WSAETIMEDOUT;
#else
    return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

void SetRecvTimeout(socket_t s, int milliseconds) {
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(milliseconds);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
}

SocketLibraryGuard::SocketLibraryGuard() {
#ifdef _WIN32
    WSADATA wsaData;
    m_initialized = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
#else
    m_initialized = true;
#endif
}

SocketLibraryGuard::~SocketLibraryGuard() {
#ifdef _WIN32
    if (m_initialized) {
        WSACleanup();
    }
#endif
}
