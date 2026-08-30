#pragma once
#include <string>
#include <vector>
#include <optional>
#include <SQLiteCpp/SQLiteCpp.h>
#include <mutex>

struct RoomRecord {
    int64_t id;
    std::string name;
    // 0 = текстовый, 1 = голосовой (см. RoomType в RoomMessages.h) — задел на
    // будущее, создание голосовых каналов пока не выставлено наружу.
    uint8_t type = 0;
};

struct ChannelOverride {
    int64_t roleId;
    uint32_t allow;
    uint32_t deny;
};

enum class RoomUpdateResult { Ok, NotFound, NameTaken, SystemRoom };
enum class RoomDeleteResult { Ok, NotFound, SystemRoom };

class RoomRepository {
public:
    explicit RoomRepository(const std::string& dbPath);

    // Возвращает id новой комнаты, либо -1, если имя занято.
    int64_t CreateRoom(const std::string& name, uint8_t type);
    // Системную комнату (is_system=1) переименовать нельзя — см. RoomUpdateResult::SystemRoom.
    RoomUpdateResult UpdateRoomName(int64_t roomId, const std::string& name);
    // Системную комнату (is_system=1) удалить нельзя — см. RoomDeleteResult::SystemRoom.
    RoomDeleteResult DeleteRoom(int64_t roomId);

    bool RoomExists(int64_t roomId);

    // false — если уже состоит в комнате
    bool AddMember(int64_t roomId, int64_t userId);
    // false — если не состоял в комнате
    bool RemoveMember(int64_t roomId, int64_t userId);

    std::vector<RoomRecord> ListRooms();
    std::vector<int64_t> ListMembers(int64_t roomId);

    std::vector<ChannelOverride> GetChannelOverrides(int64_t roomId);
    void SetChannelOverride(int64_t roomId, int64_t roleId, uint32_t allow, uint32_t deny);
    void DeleteChannelOverride(int64_t roomId, int64_t roleId);

    // Кик = BanUser + принудительный выход онлайн-пользователя на стороне gateway.
    void BanUser(int64_t roomId, int64_t userId);
    void UnbanUser(int64_t roomId, int64_t userId);
    bool IsBanned(int64_t roomId, int64_t userId);
    void MuteUser(int64_t roomId, int64_t userId);
    void UnmuteUser(int64_t roomId, int64_t userId);
    bool IsMuted(int64_t roomId, int64_t userId);

private:
    SQLite::Database m_db;
    std::mutex m_mutex;

    std::string m_sqlCreateRoom;
    std::string m_sqlUpdateRoomName;
    std::string m_sqlDeleteRoom;
    std::string m_sqlDeleteRoomMembers;
    std::string m_sqlRoomIsSystem;
    std::string m_sqlRoomExists;
    std::string m_sqlAddMember;
    std::string m_sqlRemoveMember;
    std::string m_sqlListRooms;
    std::string m_sqlListMembers;
    std::string m_sqlGetChannelOverrides;
    std::string m_sqlSetChannelOverride;
    std::string m_sqlDeleteChannelOverride;
    std::string m_sqlBanUser;
    std::string m_sqlUnbanUser;
    std::string m_sqlIsBanned;
    std::string m_sqlMuteUser;
    std::string m_sqlUnmuteUser;
    std::string m_sqlIsMuted;
};