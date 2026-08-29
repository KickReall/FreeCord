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
    // Отдельно от name: заголовок группы в панели участников на клиенте — может
    // отличаться от технического имени роли (используемого в RoleUpdateRequest и т.п.).
    std::string displayName;
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
            WriteString(buffer, role.displayName);
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
            info.displayName = ReadString(buffer, offset);
            r.roles.push_back(info);
        }
        return r;
    }
};

struct RoleCreateRequestPayload {
    std::string name;
    uint32_t permissions = 0;
    std::string displayName;  // пусто — сервер подставит name (см. UserRepository::CreateRole)

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteString(buffer, name);
        WriteScalar(buffer, permissions);
        WriteString(buffer, displayName);
        return buffer;
    }
    static RoleCreateRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoleCreateRequestPayload r;
        r.name = ReadString(buffer, offset);
        r.permissions = ReadScalar<uint32_t>(buffer, offset);
        r.displayName = ReadString(buffer, offset);
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
    std::string displayName;  // пусто — сервер подставит name

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roleId);
        WriteString(buffer, name);
        WriteScalar(buffer, permissions);
        WriteString(buffer, displayName);
        return buffer;
    }
    static RoleUpdateRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoleUpdateRequestPayload r;
        r.roleId = ReadScalar<int64_t>(buffer, offset);
        r.name = ReadString(buffer, offset);
        r.permissions = ReadScalar<uint32_t>(buffer, offset);
        r.displayName = ReadString(buffer, offset);
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

// Один пользователь для панели участников — id, имя и список назначенных ролей
// (клиент сам группирует по ролям, сопоставляя roleIds со списком из RoleListResponse).
// online заполняет только gateway (см. AnnotateOnlineStatus в gateway/main.cpp) —
// auth ничего не знает о живых сессиях, у него в БД просто не будет этого понятия;
// сам auth всегда отдаёт online=false, а gateway перезаписывает поле перед отправкой клиенту.
struct UserInfo {
    int64_t id = 0;
    std::string username;
    std::vector<int64_t> roleIds;
    bool online = false;
};

struct UserListResponsePayload {
    std::vector<UserInfo> users;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, static_cast<uint32_t>(users.size()));
        for (const auto& user : users) {
            WriteScalar(buffer, user.id);
            WriteString(buffer, user.username);
            WriteScalar(buffer, static_cast<uint32_t>(user.roleIds.size()));
            for (int64_t roleId : user.roleIds) {
                WriteScalar(buffer, roleId);
            }
            WriteScalar(buffer, static_cast<uint8_t>(user.online ? 1 : 0));
        }
        return buffer;
    }
    static UserListResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        UserListResponsePayload r;
        uint32_t count = ReadScalar<uint32_t>(buffer, offset);
        for (uint32_t i = 0; i < count; ++i) {
            UserInfo info;
            info.id = ReadScalar<int64_t>(buffer, offset);
            info.username = ReadString(buffer, offset);
            uint32_t roleCount = ReadScalar<uint32_t>(buffer, offset);
            for (uint32_t j = 0; j < roleCount; ++j) {
                info.roleIds.push_back(ReadScalar<int64_t>(buffer, offset));
            }
            info.online = ReadScalar<uint8_t>(buffer, offset) != 0;
            r.users.push_back(info);
        }
        return r;
    }
};
