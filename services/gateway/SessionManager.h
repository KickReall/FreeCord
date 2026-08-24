#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <winsock2.h>

struct Session {
    uint64_t sessionId = 0;
    int64_t userId = 0;
    std::string username;
    SOCKET socket = INVALID_SOCKET;
};

class SessionManager {
public:
    // Регистрирует нового залогиненного клиента. Возвращает сгенерированный sessionId.
    uint64_t AddSession(int64_t userId, const std::string& username, SOCKET socket);

    void RemoveSession(uint64_t sessionId);

    // Сокеты всех онлайн-пользователей из переданного списка userId.
    // Понадобится на следующем шаге, для рассылки участникам комнаты.
    std::vector<SOCKET> GetSocketsForUsers(const std::vector<int64_t>& userIds);

    size_t OnlineCount();

private:
    std::mutex m_mutex;
    std::unordered_map<uint64_t, Session> m_sessions;      // sessionId -> Session
    std::unordered_map<int64_t, uint64_t> m_userToSession; // userId -> sessionId
    uint64_t m_nextSessionId = 1;
};