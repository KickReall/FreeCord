#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <winsock2.h>

struct Session {
    uint64_t sessionId = 0;
    int64_t userId = 0;
    std::string username;
    SOCKET socket = INVALID_SOCKET;
    std::mutex sendMutex;
};

using SessionPtr = std::shared_ptr<Session>;

class SessionManager {
public:
    SessionPtr AddSession(int64_t userId, const std::string& username, SOCKET socket);
    void RemoveSession(uint64_t sessionId);
    std::vector<SessionPtr> GetSessionsForUsers(const std::vector<int64_t>& userIds);
    // Все активные сессии — для рассылки глобальных событий
    std::vector<SessionPtr> GetAllSessions();
    size_t OnlineCount();

private:
    std::mutex m_mutex;
    std::unordered_map<uint64_t, SessionPtr> m_sessions;
    std::unordered_map<int64_t, uint64_t> m_userToSession;
    std::mt19937_64 m_rng{ std::random_device{}() };
};