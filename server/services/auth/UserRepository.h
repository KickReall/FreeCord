#pragma once
#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <mutex>
#include <SQLiteCpp/SQLiteCpp.h>

struct UserRecord {
    int64_t id;
    std::string username;
    std::string passwordHash;
    std::string passwordSalt;
};

struct RoleRecord {
    int64_t id;
    std::string name;
    bool isSystem;
    uint32_t permissions;
    std::string displayName;
};

enum class RoleOpResult { Ok, NotFound, SystemRole, NameTaken };

struct UserRoleData {
    uint32_t permissions = 0;
    std::vector<int64_t> roleIds;
};

// Один пользователь для панели участников — не путать с UserRecord (там ещё
// и данные для логина, здесь только то, что нужно клиенту).
struct UserSummary {
    int64_t id;
    std::string username;
    std::vector<int64_t> roleIds;
};

class UserRepository {
public:
    explicit UserRepository(const std::string& dbPath);

    // Возвращает id нового пользователя, либо -1, если username уже занят.
    int64_t CreateUser(const std::string& username, const std::string& passwordHash, const std::string& passwordSalt);

    std::optional<UserRecord> FindByUsername(const std::string& username);
    // true — пользователь существовал и удалён (каскадом уходят и его user_roles,
    // см. FK ON DELETE CASCADE в 002_roles.sql). История сообщений не трогается —
    // sender_name там денормализован, как и при смене ника (см. CLAUDE.md).
    bool DeleteUser(int64_t userId);

    std::vector<RoleRecord> ListRoles();
    // Возвращает id новой роли, либо -1, если имя занято. Пустой displayName —
    // сервер сам подставит name (см. .cpp), чтобы не заставлять всех вызывающих
    // (включая старый test_client) обязательно придумывать отображаемое имя.
    int64_t CreateRole(const std::string& name, uint32_t permissions, const std::string& displayName);
    RoleOpResult UpdateRole(int64_t roleId, const std::string& name, uint32_t permissions, const std::string& displayName);
    RoleOpResult DeleteRole(int64_t roleId);
    std::vector<UserSummary> ListUsers();
    // false — роль уже была назначена / не была назначена
    bool AssignRole(int64_t userId, int64_t roleId);
    bool RemoveRole(int64_t userId, int64_t roleId);
    // Объединение прав всех ролей пользователя (admin — особый случай, см. Permissions.h)
    // и список id этих ролей — раньше были двумя отдельными запросами по одному и
    // тому же user_roles JOIN roles, слиты в один проход по тем же строкам.
    UserRoleData GetUserRoleData(int64_t userId);

    void BanIp(const std::string& ip);
    void UnbanIp(const std::string& ip);
    bool IsIpBanned(const std::string& ip);
    std::vector<std::string> ListBannedIps();

private:
    SQLite::Database m_db;
    std::mutex m_mutex;
    std::string m_sqlCreateUser;
    std::string m_sqlFindByUsername;
    std::string m_sqlDeleteUser;
    std::string m_sqlCountUsers;
    std::string m_sqlAssignRole;
    std::string m_sqlListRoles;
    std::string m_sqlListUsers;
    std::string m_sqlCreateRole;
    std::string m_sqlFindRoleById;
    std::string m_sqlUpdateRole;
    std::string m_sqlDeleteRole;
    std::string m_sqlRemoveRole;
    std::string m_sqlGetUserRolePermissions;
    std::string m_sqlBanIp;
    std::string m_sqlUnbanIp;
    std::string m_sqlIsIpBanned;
    std::string m_sqlListBannedIps;
};
