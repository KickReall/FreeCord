#include "PlatformSocket.h"
#include <cstdint>

#ifndef _WIN32
    #include <cerrno>
    #include <csignal>
#endif

void CloseSocket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

void ShutdownSocket(socket_t s) {
#ifdef _WIN32
    shutdown(s, SD_BOTH);
#else
    shutdown(s, SHUT_RDWR);
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

socket_t CreateListenSocket(int port) {
    socket_t listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == kInvalidSocket) return kInvalidSocket;

    int reuse = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        CloseSocket(listenSocket);
        return kInvalidSocket;
    }
    listen(listenSocket, SOMAXCONN);
    return listenSocket;
}

SocketLibraryGuard::SocketLibraryGuard() {
#ifdef _WIN32
    WSADATA wsaData;
    m_initialized = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
#else
    // Без этого запись в сокет, который уже закрыт на другом конце (или сами
    // прервали через ShutdownSocket — например, при бане по IP), убивает весь
    // процесс сигналом SIGPIPE по умолчанию. Windows такого сигнала не знает —
    // там send() на такой сокет просто возвращает SOCKET_ERROR, поэтому баг
    // проявлялся только на Linux/WSL.
    signal(SIGPIPE, SIG_IGN);
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
