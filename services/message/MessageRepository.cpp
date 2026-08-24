#include "MessageRepository.h"
#include <algorithm>

MessageRepository::MessageRepository(const std::string& dbPath)
    : m_db(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_db.exec("PRAGMA journal_mode = WAL");
    m_db.exec("PRAGMA busy_timeout = 5000");

    m_db.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            room_id INTEGER NOT NULL,
            sender_id INTEGER NOT NULL,
            sender_name TEXT NOT NULL DEFAULT '',
            text TEXT NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )");

    // Индекс критичен: без него выборка истории комнаты будет сканировать всю таблицу целиком.
    m_db.exec("CREATE INDEX IF NOT EXISTS idx_messages_room_time ON messages(room_id, timestamp)");
}

int64_t MessageRepository::SaveMessage(int64_t roomId, int64_t senderId, const std::string& senderName,
    const std::string& text, int64_t timestamp) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(m_db,
            "INSERT INTO messages (room_id, sender_id, sender_name, text, timestamp) VALUES (?, ?, ?, ?, ?)");
        query.bind(1, roomId);
        query.bind(2, senderId);
        query.bind(3, senderName);
        query.bind(4, text);
        query.bind(5, timestamp);
        query.exec();
        return m_db.getLastInsertRowid();
    }
    catch (const SQLite::Exception&) {
        return -1;
    }
}

std::vector<ChatMessage> MessageRepository::GetHistory(int64_t roomId, uint32_t limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ChatMessage> result;

    // Берём ПОСЛЕДНИЕ limit сообщений — сортируем по убыванию, потом переворачиваем.
    SQLite::Statement query(m_db,
        "SELECT id, room_id, sender_id, sender_name, timestamp, text FROM messages "
        "WHERE room_id = ? ORDER BY timestamp DESC, id DESC LIMIT ?");
    query.bind(1, roomId);
    query.bind(2, static_cast<int>(limit));

    while (query.executeStep()) {
        ChatMessage msg;
        msg.id = query.getColumn(0).getInt64();
        msg.roomId = query.getColumn(1).getInt64();
        msg.senderId = query.getColumn(2).getInt64();
        msg.senderName = query.getColumn(3).getString();
        msg.timestamp = query.getColumn(4).getInt64();
        msg.text = query.getColumn(5).getString();
        result.push_back(msg);
    }

    // Разворачиваем: клиенту удобнее получать от старых к новым (как в чате сверху вниз).
    std::reverse(result.begin(), result.end());
    return result;
}