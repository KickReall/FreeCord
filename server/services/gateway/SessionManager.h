#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "Transport.h"
#include <atomic>

struct Session {
    uint64_t sessionId = 0;
    int64_t userId = 0;
    std::string username;
    std::shared_ptr<ITransport> transport;
    std::mutex sendMutex;

    // Комната, открытая пользователем сейчас. 0 = ни одной.
    // atomic — читается из чужих потоков при рассылке.
    std::atomic<int64_t> currentRoomId{ 0 };

    // Эффективные права пользователя, посчитанные один раз при логине (сумма прав
    // всех его ролей). Изменение ролей применится только после повторного логина —
    // живое обновление сессии не реализовано.
    std::atomic<uint32_t> permissions{ 0 };
};

using SessionPtr = std::shared_ptr<Session>;

class SessionManager {
public:
    SessionPtr AddSession(int64_t userId, const std::string& username, std::shared_ptr<ITransport> transport);
    void RemoveSession(uint64_t sessionId);
    std::vector<SessionPtr> GetSessionsForUsers(const std::vector<int64_t>& userIds);
    // Все активные сессии — для рассылки глобальных событий
    std::vector<SessionPtr> GetAllSessions();
    // Все, кто сейчас открыл эту комнату
    std::vector<SessionPtr> GetSessionsInRoom(int64_t roomId);
    size_t OnlineCount();

private:
    std::mutex m_mutex;
    std::unordered_map<uint64_t, SessionPtr> m_sessions;
    std::unordered_map<int64_t, uint64_t> m_userToSession;
    std::mt19937_64 m_rng{ std::random_device{}() };
};