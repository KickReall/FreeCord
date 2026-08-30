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
    m_sqlUpdateRoomName = LoadSqlFile("db/room/queries/update_room_name.sql");
    m_sqlDeleteRoom = LoadSqlFile("db/room/queries/delete_room.sql");
    m_sqlDeleteRoomMembers = LoadSqlFile("db/room/queries/delete_room_members.sql");
    m_sqlRoomIsSystem = LoadSqlFile("db/room/queries/room_is_system.sql");
    m_sqlRoomExists = LoadSqlFile("db/room/queries/room_exists.sql");
    m_sqlAddMember = LoadSqlFile("db/room/queries/add_member.sql");
    m_sqlRemoveMember = LoadSqlFile("db/room/queries/remove_member.sql");
    m_sqlListRooms = LoadSqlFile("db/room/queries/list_rooms.sql");
    m_sqlListMembers = LoadSqlFile("db/room/queries/list_members.sql");
    m_sqlGetChannelOverrides = LoadSqlFile("db/room/queries/get_channel_overrides.sql");
    m_sqlSetChannelOverride = LoadSqlFile("db/room/queries/set_channel_override.sql");
    m_sqlDeleteChannelOverride = LoadSqlFile("db/room/queries/delete_channel_override.sql");
    m_sqlBanUser = LoadSqlFile("db/room/queries/ban_user.sql");
    m_sqlUnbanUser = LoadSqlFile("db/room/queries/unban_user.sql");
    m_sqlIsBanned = LoadSqlFile("db/room/queries/is_banned.sql");
    m_sqlMuteUser = LoadSqlFile("db/room/queries/mute_user.sql");
    m_sqlUnmuteUser = LoadSqlFile("db/room/queries/unmute_user.sql");
    m_sqlIsMuted = LoadSqlFile("db/room/queries/is_muted.sql");
}

int64_t RoomRepository::CreateRoom(const std::string& name, uint8_t type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(m_db, m_sqlCreateRoom);
        query.bind(1, name);
        query.bind(2, static_cast<int>(type));
        query.exec();
        return m_db.getLastInsertRowid();
    }
    catch (const SQLite::Exception&) {
        return -1; // UNIQUE constraint — имя занято
    }
}

RoomUpdateResult RoomRepository::UpdateRoomName(int64_t roomId, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement systemQuery(m_db, m_sqlRoomIsSystem);
    systemQuery.bind(1, roomId);
    if (!systemQuery.executeStep()) return RoomUpdateResult::NotFound;
    if (systemQuery.getColumn(0).getInt() != 0) return RoomUpdateResult::SystemRoom;

    try {
        SQLite::Statement updateQuery(m_db, m_sqlUpdateRoomName);
        updateQuery.bind(1, name);
        updateQuery.bind(2, roomId);
        updateQuery.exec();
        return RoomUpdateResult::Ok;
    }
    catch (const SQLite::Exception&) {
        return RoomUpdateResult::NameTaken; // UNIQUE constraint
    }
}

RoomDeleteResult RoomRepository::DeleteRoom(int64_t roomId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement systemQuery(m_db, m_sqlRoomIsSystem);
    systemQuery.bind(1, roomId);
    if (!systemQuery.executeStep()) return RoomDeleteResult::NotFound;
    if (systemQuery.getColumn(0).getInt() != 0) return RoomDeleteResult::SystemRoom;

    // room_members — единственная из связанных таблиц без ON DELETE CASCADE (её FK
    // старше остальных, из 001_initial.sql, до того как каскад вошёл в привычку) —
    // чистим вручную, иначе внешний ключ не даст удалить комнату. channel_role_overrides/
    // channel_bans/channel_mutes (миграции 002/003) каскадятся сами. Историю сообщений
    // (другая БД) не трогаем — как и при удалении пользователя, sender_name/roomId там денормализованы.
    SQLite::Statement clearMembers(m_db, m_sqlDeleteRoomMembers);
    clearMembers.bind(1, roomId);
    clearMembers.exec();

    SQLite::Statement deleteQuery(m_db, m_sqlDeleteRoom);
    deleteQuery.bind(1, roomId);
    deleteQuery.exec();
    return RoomDeleteResult::Ok;
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
        record.type = static_cast<uint8_t>(query.getColumn(2).getInt());
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

std::vector<ChannelOverride> RoomRepository::GetChannelOverrides(int64_t roomId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ChannelOverride> result;
    SQLite::Statement query(m_db, m_sqlGetChannelOverrides);
    query.bind(1, roomId);
    while (query.executeStep()) {
        ChannelOverride o;
        o.roleId = query.getColumn(0).getInt64();
        o.allow = static_cast<uint32_t>(query.getColumn(1).getInt64());
        o.deny = static_cast<uint32_t>(query.getColumn(2).getInt64());
        result.push_back(o);
    }
    return result;
}

void RoomRepository::SetChannelOverride(int64_t roomId, int64_t roleId, uint32_t allow, uint32_t deny) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlSetChannelOverride);
    query.bind(1, roomId);
    query.bind(2, roleId);
    query.bind(3, static_cast<int64_t>(allow));
    query.bind(4, static_cast<int64_t>(deny));
    query.exec();
}

void RoomRepository::DeleteChannelOverride(int64_t roomId, int64_t roleId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlDeleteChannelOverride);
    query.bind(1, roomId);
    query.bind(2, roleId);
    query.exec();
}

void RoomRepository::BanUser(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlBanUser);
    query.bind(1, roomId);
    query.bind(2, userId);
    query.exec();
}

void RoomRepository::UnbanUser(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlUnbanUser);
    query.bind(1, roomId);
    query.bind(2, userId);
    query.exec();
}

bool RoomRepository::IsBanned(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlIsBanned);
    query.bind(1, roomId);
    query.bind(2, userId);
    return query.executeStep();
}

void RoomRepository::MuteUser(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlMuteUser);
    query.bind(1, roomId);
    query.bind(2, userId);
    query.exec();
}

void RoomRepository::UnmuteUser(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlUnmuteUser);
    query.bind(1, roomId);
    query.bind(2, userId);
    query.exec();
}

bool RoomRepository::IsMuted(int64_t roomId, int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlIsMuted);
    query.bind(1, roomId);
    query.bind(2, userId);
    return query.executeStep();
}
