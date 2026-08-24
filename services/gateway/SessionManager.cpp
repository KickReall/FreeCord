#include "SessionManager.h"

uint64_t SessionManager::AddSession(int64_t userId, const std::string& username, SOCKET socket) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Если пользователь уже был онлайн (переподключился), старую сессию выбрасываем.
    auto existing = m_userToSession.find(userId);
    if (existing != m_userToSession.end()) {
        m_sessions.erase(existing->second);
    }

    uint64_t sessionId = m_nextSessionId++;
    Session session;
    session.sessionId = sessionId;
    session.userId = userId;
    session.username = username;
    session.socket = socket;

    m_sessions[sessionId] = session;
    m_userToSession[userId] = sessionId;
    return sessionId;
}

void SessionManager::RemoveSession(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return;

    m_userToSession.erase(it->second.userId);
    m_sessions.erase(it);
}

std::vector<SOCKET> SessionManager::GetSocketsForUsers(const std::vector<int64_t>& userIds) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<SOCKET> sockets;
    for (int64_t userId : userIds) {
        auto sessionIt = m_userToSession.find(userId);
        if (sessionIt == m_userToSession.end()) continue; // не онлайн

        auto it = m_sessions.find(sessionIt->second);
        if (it != m_sessions.end()) {
            sockets.push_back(it->second.socket);
        }
    }
    return sockets;
}

size_t SessionManager::OnlineCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessions.size();
}