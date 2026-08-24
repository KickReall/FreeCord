#include "SessionManager.h"

SessionPtr SessionManager::AddSession(int64_t userId, const std::string& username, SOCKET socket) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto existing = m_userToSession.find(userId);
    if (existing != m_userToSession.end()) {
        m_sessions.erase(existing->second);
    }

    auto session = std::make_shared<Session>();
    session->sessionId = m_nextSessionId++;
    session->userId = userId;
    session->username = username;
    session->socket = socket;

    m_sessions[session->sessionId] = session;
    m_userToSession[userId] = session->sessionId;
    return session;
}

void SessionManager::RemoveSession(uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) return;

    m_userToSession.erase(it->second->userId);
    m_sessions.erase(it);
}

std::vector<SessionPtr> SessionManager::GetSessionsForUsers(const std::vector<int64_t>& userIds) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<SessionPtr> result;
    for (int64_t userId : userIds) {
        auto sessionIt = m_userToSession.find(userId);
        if (sessionIt == m_userToSession.end()) continue;

        auto it = m_sessions.find(sessionIt->second);
        if (it != m_sessions.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

size_t SessionManager::OnlineCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessions.size();
}