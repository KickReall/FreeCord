#include "RoomRepository.h"
#include "MigrationRunner.h"
#include "SqlFile.h"

RoomRepository::RoomRepository(const std::string& dbPath)
    : m_db(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // WAL позволяет читать во время записи — пригодится, когда сервисы начнут работать параллельно
    m_db.exec("PRAGMA journal_mode = WAL");
    m_db.exec("PRAGMA busy_timeout = 5000");
    m_db.exec("PRAGMA foreign_keys = ON");

    ApplyMigrations(m_db, LoadMigrationsFromDirectory("db/room/migrations"));

    m_sqlCreateRoom = LoadSqlFile("db/room/queries/create_room.sql");
    m_sqlRoomExists = LoadSqlFile("db/room/queries/room_exists.sql");
    m_sqlAddMember = LoadSqlFile("db/room/queries/add_member.sql");
    m_sqlRemoveMember = LoadSqlFile("db/room/queries/remove_member.sql");
    m_sqlListRooms = LoadSqlFile("db/room/queries/list_rooms.sql");
    m_sqlListMembers = LoadSqlFile("db/room/queries/list_members.sql");
}

int64_t RoomRepository::CreateRoom(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(m_db, m_sqlCreateRoom);
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
    SQLite::Statement query(m_db, m_sqlRoomExists);
    query.bind(1, roomId);
    return query.executeStep();
}

bool RoomRepository::AddMember(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(m_db, m_sqlAddMember);
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
    SQLite::Statement query(m_db, m_sqlRemoveMember);
    query.bind(1, roomId);
    query.bind(2, userId);
    return query.exec() > 0; // exec() возвращает число затронутых строк
}

std::vector<RoomRecord> RoomRepository::ListRooms() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<RoomRecord> result;
    SQLite::Statement query(m_db, m_sqlListRooms);
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
    SQLite::Statement query(m_db, m_sqlListMembers);
    query.bind(1, roomId);
    while (query.executeStep()) {
        result.push_back(query.getColumn(0).getInt64());
    }
    return result;
}
