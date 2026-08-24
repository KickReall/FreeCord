#pragma once
#include <string>
#include <vector>
#include <SQLiteCpp/SQLiteCpp.h>
#include "MessageMessages.h"
#include <mutex>

class MessageRepository {
public:
    explicit MessageRepository(const std::string& dbPath);

    // Возвращает id нового сообщения, либо -1 при ошибке.
    int64_t SaveMessage(int64_t roomId, int64_t senderId, const std::string& senderName,
        const std::string& text, int64_t timestamp);

    // Последние `limit` сообщений комнаты, отсортированные от старых к новым.
    std::vector<ChatMessage> GetHistory(int64_t roomId, uint32_t limit);

private:
    SQLite::Database m_db;
    std::mutex m_mutex;
};