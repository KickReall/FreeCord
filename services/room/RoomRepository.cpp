#include "RoomRepository.h"

RoomRepository::RoomRepository(const std::string& dbPath)
    : m_db(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // WAL позволяет читать во время записи — пригодится, когда сервисы начнут работать параллельно
    m_db.exec("PRAGMA journal_mode = WAL");
    m_db.exec("PRAGMA busy_timeout = 5000");
    m_db.exec("PRAGMA foreign_keys = ON");

    m_db.exec(R"(
        CREATE TABLE IF NOT EXISTS rooms (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )");

    m_db.exec(R"(
        CREATE TABLE IF NOT EXISTS room_members (
            room_id INTEGER NOT NULL REFERENCES rooms(id),
            user_id INTEGER NOT NULL,
            joined_at TEXT NOT NULL DEFAULT (datetime('now')),
            PRIMARY KEY (room_id, user_id)
        )
    )");
}

int64_t RoomRepository::CreateRoom(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(m_db, "INSERT INTO rooms (name) VALUES (?)");
        query.bind(1, name);
        query.exec();
        return m_db.getLastInsertRowid();
    }
    catch (const SQLite::Exception&) {
        return -1; // UNIQUE constraint — имя занято
    }
}

bool RoomRepository::RoomExists(int64_t roomId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, "SELECT 1 FROM rooms WHERE id = ?");
    query.bind(1, roomId);
    return query.executeStep();
}

bool RoomRepository::AddMember(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(m_db, "INSERT INTO room_members (room_id, user_id) VALUES (?, ?)");
        query.bind(1, roomId);
        query.bind(2, userId);
        query.exec();
        return true;
    }
    catch (const SQLite::Exception&) {
        return false; // уже состоит (PRIMARY KEY) или комнаты нет (FOREIGN KEY)
    }
}

bool RoomRepository::RemoveMember(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, "DELETE FROM room_members WHERE room_id = ? AND user_id = ?");
    query.bind(1, roomId);
    query.bind(2, userId);
    return query.exec() > 0; // exec() возвращает число затронутых строк
}

std::vector<RoomRecord> RoomRepository::ListRooms() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<RoomRecord> result;
    SQLite::Statement query(m_db, "SELECT id, name FROM rooms ORDER BY id");
    while (query.executeStep()) {
        RoomRecord record;
        record.id = query.getColumn(0).getInt64();
        record.name = query.getColumn(1).getString();
        result.push_back(record);
    }
    return result;
}

std::vector<int64_t> RoomRepository::ListMembers(int64_t roomId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<int64_t> result;
    SQLite::Statement query(m_db, "SELECT user_id FROM room_members WHERE room_id = ? ORDER BY user_id");
    query.bind(1, roomId);
    while (query.executeStep()) {
        result.push_back(query.getColumn(0).getInt64());
    }
    return result;
}