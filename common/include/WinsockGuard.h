#pragma once
#include <winsock2.h>

// RAII-обёртка над WSAStartup/WSACleanup — инициализация происходит в конструкторе,
// деинициализация — автоматически в деструкторе при выходе из области видимости.
class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA wsaData;
        m_initialized = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    }
    ~WinsockGuard() {
        if (m_initialized) {
            WSACleanup();
        }
    }
    bool IsInitialized() const { return m_initialized; }

    WinsockGuard(const WinsockGuard&) = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;

private:
    bool m_initialized = false;
};