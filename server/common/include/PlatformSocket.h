#pragma once

// Тонкий слой над различиями Winsock2 (Windows) и BSD sockets (Linux).
// Сам API вызовов (socket/bind/listen/accept/connect/send/recv) одинаков на
// обеих платформах — обёртка нужна только вокруг типов, констант и обвязки,
// которые расходятся.

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>

    using socket_t = SOCKET;
    constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>

    using socket_t = int;
    constexpr socket_t kInvalidSocket = -1;
#endif

// closesocket() / close()
void CloseSocket(socket_t s);

// shutdown() — обрывает блокирующий recv() в потоке-владельце сокета, вызывается
// из ДРУГОГО потока (например, при бане по IP); сам fd не закрывает — закрытие
// остаётся за потоком-владельцем, как и раньше.
void ShutdownSocket(socket_t s);

// WSAGetLastError() / errno
int GetLastSocketError();

// WSAETIMEDOUT / EAGAIN || EWOULDBLOCK — recv() отдаёт разные коды для таймаута
bool IsTimeoutError(int err);

// Прячет разницу между DWORD (мс) на Windows и struct timeval на Linux
void SetRecvTimeout(socket_t s, int milliseconds);

// Создаёт TCP-сокет, включает SO_REUSEADDR (иначе перезапуск сразу после
// остановки сервиса упирается в TIME_WAIT — порт "занят" ещё ~минуту) и биндит
// его на INADDR_ANY:port. Возвращает kInvalidSocket, если bind() не удался —
// сообщение со своим префиксом печатает вызывающая сторона (у каждого сервиса свой).
socket_t CreateListenSocket(int port);

// RAII над инициализацией сокетной библиотеки.
// На Windows — WSAStartup/WSACleanup, на Linux — no-op (не требуется).
class SocketLibraryGuard {
public:
    SocketLibraryGuard();
    ~SocketLibraryGuard();

    bool IsInitialized() const { return m_initialized; }

    SocketLibraryGuard(const SocketLibraryGuard&) = delete;
    SocketLibraryGuard& operator=(const SocketLibraryGuard&) = delete;

private:
    bool m_initialized = false;
};
