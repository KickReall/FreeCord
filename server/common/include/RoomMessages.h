#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Serialization.h"

struct RoomCreateRequestPayload {
    std::string name;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteString(buffer, name);
        return buffer;
    }
    static RoomCreateRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomCreateRequestPayload r;
        r.name = ReadString(buffer, offset);
        return r;
    }
};

struct RoomCreateResponsePayload {
    uint8_t status = 0;   // 0 = ok, 1 = name taken
    int64_t roomId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, status);
        WriteScalar(buffer, roomId);
        return buffer;
    }
    static RoomCreateResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomCreateResponsePayload r;
        r.status = ReadScalar<uint8_t>(buffer, offset);
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

// Используется и для Join, и для Leave — payload одинаковый
struct RoomMembershipRequestPayload {
    int64_t roomId = 0;
    int64_t userId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteScalar(buffer, userId);
        return buffer;
    }
    static RoomMembershipRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomMembershipRequestPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.userId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

struct StatusResponsePayload {
    uint8_t status = 0;   // 0 = ok, 1 = room not found, 2 = already member / not a member

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, status);
        return buffer;
    }
    static StatusResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        StatusResponsePayload r;
        r.status = ReadScalar<uint8_t>(buffer, offset);
        return r;
    }
};

struct RoomInfo {
    int64_t id = 0;
    std::string name;
};

struct RoomListResponsePayload {
    std::vector<RoomInfo> rooms;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, static_cast<uint32_t>(rooms.size()));
        for (const auto& room : rooms) {
            WriteScalar(buffer, room.id);
            WriteString(buffer, room.name);
        }
        return buffer;
    }
    static RoomListResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomListResponsePayload r;
        uint32_t count = ReadScalar<uint32_t>(buffer, offset);
        for (uint32_t i = 0; i < count; ++i) {
            RoomInfo info;
            info.id = ReadScalar<int64_t>(buffer, offset);
            info.name = ReadString(buffer, offset);
            r.rooms.push_back(info);
        }
        return r;
    }
};

struct RoomMembersRequestPayload {
    int64_t roomId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        return buffer;
    }
    static RoomMembersRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomMembersRequestPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

struct RoomMembersResponsePayload {
    std::vector<int64_t> userIds;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, static_cast<uint32_t>(userIds.size()));
        for (int64_t id : userIds) {
            WriteScalar(buffer, id);
        }
        return buffer;
    }
    static RoomMembersResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomMembersResponsePayload r;
        uint32_t count = ReadScalar<uint32_t>(buffer, offset);
        for (uint32_t i = 0; i < count; ++i) {
            r.userIds.push_back(ReadScalar<int64_t>(buffer, offset));
        }
        return r;
    }
};

struct ChannelOverrideInfo {
    int64_t roleId = 0;
    uint32_t allow = 0;
    uint32_t deny = 0;
};

struct ChannelOverridesRequestPayload {
    int64_t roomId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        return buffer;
    }
    static ChannelOverridesRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        ChannelOverridesRequestPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

struct ChannelOverridesResponsePayload {
    std::vector<ChannelOverrideInfo> overrides;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, static_cast<uint32_t>(overrides.size()));
        for (const auto& o : overrides) {
            WriteScalar(buffer, o.roleId);
            WriteScalar(buffer, o.allow);
            WriteScalar(buffer, o.deny);
        }
        return buffer;
    }
    static ChannelOverridesResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        ChannelOverridesResponsePayload r;
        uint32_t count = ReadScalar<uint32_t>(buffer, offset);
        for (uint32_t i = 0; i < count; ++i) {
            ChannelOverrideInfo o;
            o.roleId = ReadScalar<int64_t>(buffer, offset);
            o.allow = ReadScalar<uint32_t>(buffer, offset);
            o.deny = ReadScalar<uint32_t>(buffer, offset);
            r.overrides.push_back(o);
        }
        return r;
    }
};

struct SetChannelOverrideRequestPayload {
    int64_t roomId = 0;
    int64_t roleId = 0;
    uint32_t allow = 0;
    uint32_t deny = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteScalar(buffer, roleId);
        WriteScalar(buffer, allow);
        WriteScalar(buffer, deny);
        return buffer;
    }
    static SetChannelOverrideRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        SetChannelOverrideRequestPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.roleId = ReadScalar<int64_t>(buffer, offset);
        r.allow = ReadScalar<uint32_t>(buffer, offset);
        r.deny = ReadScalar<uint32_t>(buffer, offset);
        return r;
    }
};

// Сброс оверрайда роли на канале обратно к базовым правам роли (без исключения из canала).
struct DeleteChannelOverrideRequestPayload {
    int64_t roomId = 0;
    int64_t roleId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteScalar(buffer, roleId);
        return buffer;
    }
    static DeleteChannelOverrideRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        DeleteChannelOverrideRequestPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.roleId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};