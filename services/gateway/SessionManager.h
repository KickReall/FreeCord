#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <winsock2.h>

// Сессия хранится через shared_ptr: поток-отправитель держит её живой,
// даже если владелец сокета отключился в этот момент.
struct Session {
    uint64_t sessionId = 0;
    int64_t userId = 0;
    std::string username;
    SOCKET socket = INVALID_SOCKET;
    std::mutex sendMutex;   // сериализует запись в этот сокет
};

using SessionPtr = std::shared_ptr<Session>;

class SessionManager {
public:
    SessionPtr AddSession(int64_t userId, const std::string& username, SOCKET socket);
    void RemoveSession(uint64_t sessionId);

    // Сессии тех из userIds, кто сейчас онлайн.
    std::vector<SessionPtr> GetSessionsForUsers(const std::vector<int64_t>& userIds);

    size_t OnlineCount();

private:
    std::mutex m_mutex;
    std::unordered_map<uint64_t, SessionPtr> m_sessions;
    std::unordered_map<int64_t, uint64_t> m_userToSession;
    uint64_t m_nextSessionId = 1;
};