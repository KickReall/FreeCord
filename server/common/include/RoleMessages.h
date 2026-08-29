#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Serialization.h"

struct RoleInfo {
    int64_t id = 0;
    std::string name;
    bool isSystem = false;
    uint32_t permissions = 0;
};

struct RoleListResponsePayload {
    std::vector<RoleInfo> roles;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, static_cast<uint32_t>(roles.size()));
        for (const auto& role : roles) {
            WriteScalar(buffer, role.id);
            WriteString(buffer, role.name);
            WriteScalar(buffer, static_cast<uint8_t>(role.isSystem ? 1 : 0));
            WriteScalar(buffer, role.permissions);
        }
        return buffer;
    }
    static RoleListResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoleListResponsePayload r;
        uint32_t count = ReadScalar<uint32_t>(buffer, offset);
        for (uint32_t i = 0; i < count; ++i) {
            RoleInfo info;
            info.id = ReadScalar<int64_t>(buffer, offset);
            info.name = ReadString(buffer, offset);
            info.isSystem = ReadScalar<uint8_t>(buffer, offset) != 0;
            info.permissions = ReadScalar<uint32_t>(buffer, offset);
            r.roles.push_back(info);
        }
        return r;
    }
};

struct RoleCreateRequestPayload {
    std::string name;
    uint32_t permissions = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteString(buffer, name);
        WriteScalar(buffer, permissions);
        return buffer;
    }
    static RoleCreateRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoleCreateRequestPayload r;
        r.name = ReadString(buffer, offset);
        r.permissions = ReadScalar<uint32_t>(buffer, offset);
        return r;
    }
};

struct RoleCreateResponsePayload {
    uint8_t status = 0;  // 0 = ok, 1 = имя занято, 254 = недостаточно прав
    int64_t roleId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, status);
        WriteScalar(buffer, roleId);
        return buffer;
    }
    static RoleCreateResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoleCreateResponsePayload r;
        r.status = ReadScalar<uint8_t>(buffer, offset);
        r.roleId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

struct RoleUpdateRequestPayload {
    int64_t roleId = 0;
    std::string name;
    uint32_t permissions = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roleId);
        WriteString(buffer, name);
        WriteScalar(buffer, permissions);
        return buffer;
    }
    static RoleUpdateRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoleUpdateRequestPayload r;
        r.roleId = ReadScalar<int64_t>(buffer, offset);
        r.name = ReadString(buffer, offset);
        r.permissions = ReadScalar<uint32_t>(buffer, offset);
        return r;
    }
};

struct RoleDeleteRequestPayload {
    int64_t roleId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roleId);
        return buffer;
    }
    static RoleDeleteRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoleDeleteRequestPayload r;
        r.roleId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

// Общий payload для Assign и Remove — набор полей одинаковый.
struct RoleMembershipRequestPayload {
    int64_t userId = 0;
    int64_t roleId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, userId);
        WriteScalar(buffer, roleId);
        return buffer;
    }
    static RoleMembershipRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoleMembershipRequestPayload r;
        r.userId = ReadScalar<int64_t>(buffer, offset);
        r.roleId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

// Эффективные права пользователя (объединение прав всех его ролей) и список id
// самих ролей — нужен gateway'ю, чтобы считать оверрайды по каналам (роль в
// оверрайде может не совпадать с "суммой прав", поэтому одной суммы мало).
// Используется и как внутренний ответ auth -> gateway, и как push gateway -> клиент после логина.
struct MyPermissionsPayload {
    uint32_t permissions = 0;
    std::vector<int64_t> roleIds;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, permissions);
        WriteScalar(buffer, static_cast<uint32_t>(roleIds.size()));
        for (int64_t roleId : roleIds) {
            WriteScalar(buffer, roleId);
        }
        return buffer;
    }
    static MyPermissionsPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        MyPermissionsPayload r;
        r.permissions = ReadScalar<uint32_t>(buffer, offset);
        uint32_t count = ReadScalar<uint32_t>(buffer, offset);
        for (uint32_t i = 0; i < count; ++i) {
            r.roleIds.push_back(ReadScalar<int64_t>(buffer, offset));
        }
        return r;
    }
};

// Внутренний запрос gateway -> auth: посчитать эффективные права пользователя.
struct GetUserPermissionsRequestPayload {
    int64_t userId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, userId);
        return buffer;
    }
    static GetUserPermissionsRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        GetUserPermissionsRequestPayload r;
        r.userId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};
