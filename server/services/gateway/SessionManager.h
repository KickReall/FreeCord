#pragma once
#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "Transport.h"
#include "PlatformSocket.h"
#include <atomic>

struct Session {
    uint64_t sessionId = 0;
    int64_t userId = 0;
    std::string username;
    std::shared_ptr<ITransport> transport;
    std::mutex sendMutex;

    // IP клиента и сырой сокет — нужны для бана по IP: найти сессии с этим адресом
    // и прервать их немедленно через ShutdownSocket, независимо от TLS-обёртки.
    // Оба пишутся один раз при создании сессии, как username — без мьютекса.
    std::string remoteIp;
    socket_t rawSocket = kInvalidSocket;

    // Комната, открытая пользователем сейчас. 0 = ни одной.
    // atomic — читается из чужих потоков при рассылке.
    std::atomic<int64_t> currentRoomId{ 0 };

    // Эффективные права пользователя, посчитанные один раз при логине (сумма прав
    // всех его ролей). Изменение ролей применится только после повторного логина —
    // живое обновление сессии не реализовано.
    std::atomic<uint32_t> permissions{ 0 };

    // Id ролей пользователя — нужны, чтобы считать оверрайды прав по каналам
    // (одной суммы permissions недостаточно, оверрайд привязан к конкретной роли).
    // Пишется один раз при логине, как и username — без мьютекса, как и оно.
    std::vector<int64_t> roleIds;

    // Антифлуд (token bucket, см. TryConsumeRateLimitToken в gateway/main.cpp) —
    // отдельные бакеты на обычные сообщения и на "печатает", т.к. у них разная
    // естественная частота. Читаются и пишутся только собственным потоком сессии
    // (ClientThread обрабатывает кадры этого клиента строго последовательно),
    // поэтому, как и roleIds/username, без мьютекса. lastRefill остаётся в
    // "нулевом" состоянии (epoch) до первого вызова — это не баг: формула
    // пополнения сама досчитает бакет до полного на первом же обращении.
    double messageTokens = 0.0;
    double typingTokens = 0.0;
    std::chrono::steady_clock::time_point lastMessageRefill;
    std::chrono::steady_clock::time_point lastTypingRefill;
};

using SessionPtr = std::shared_ptr<Session>;

class SessionManager {
public:
    SessionPtr AddSession(int64_t userId, const std::string& username, std::shared_ptr<ITransport> transport,
        const std::string& remoteIp, socket_t rawSocket);
    void RemoveSession(uint64_t sessionId);
    std::vector<SessionPtr> GetSessionsForUsers(const std::vector<int64_t>& userIds);
    // Все сессии, подключённые с этого IP — для немедленного разрыва при бане.
    std::vector<SessionPtr> GetSessionsForIp(const std::string& ip);
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