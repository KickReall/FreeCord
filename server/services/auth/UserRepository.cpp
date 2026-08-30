#include "UserRepository.h"
#include "MigrationRunner.h"
#include "SqlFile.h"
#include "Permissions.h"
#include <filesystem>
#include <fstream>

UserRepository::UserRepository(const std::string& dbPath, const std::string& avatarDir)
    : m_db(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
    , m_avatarDir(avatarDir)
{
    m_db.exec("PRAGMA foreign_keys = ON");
    std::filesystem::create_directories(m_avatarDir);

    ApplyMigrations(m_db, LoadMigrationsFromDirectory("db/auth/migrations"));

    m_sqlCreateUser = LoadSqlFile("db/auth/queries/create_user.sql");
    m_sqlFindByUsername = LoadSqlFile("db/auth/queries/find_by_username.sql");
    m_sqlDeleteUser = LoadSqlFile("db/auth/queries/delete_user.sql");
    m_sqlCountUsers = LoadSqlFile("db/auth/queries/count_users.sql");
    m_sqlAssignRole = LoadSqlFile("db/auth/queries/assign_role.sql");
    m_sqlListRoles = LoadSqlFile("db/auth/queries/list_roles.sql");
    m_sqlListUsers = LoadSqlFile("db/auth/queries/list_users.sql");
    m_sqlCreateRole = LoadSqlFile("db/auth/queries/create_role.sql");
    m_sqlFindRoleById = LoadSqlFile("db/auth/queries/find_role_by_id.sql");
    m_sqlUpdateRole = LoadSqlFile("db/auth/queries/update_role.sql");
    m_sqlDeleteRole = LoadSqlFile("db/auth/queries/delete_role.sql");
    m_sqlRemoveRole = LoadSqlFile("db/auth/queries/remove_role.sql");
    m_sqlGetUserRolePermissions = LoadSqlFile("db/auth/queries/get_user_role_permissions.sql");
    m_sqlBanIp = LoadSqlFile("db/auth/queries/ban_ip.sql");
    m_sqlUnbanIp = LoadSqlFile("db/auth/queries/unban_ip.sql");
    m_sqlIsIpBanned = LoadSqlFile("db/auth/queries/is_ip_banned.sql");
    m_sqlListBannedIps = LoadSqlFile("db/auth/queries/list_banned_ips.sql");
    m_sqlBumpAvatarVersion = LoadSqlFile("db/auth/queries/bump_avatar_version.sql");
    m_sqlGetAvatarVersion = LoadSqlFile("db/auth/queries/get_avatar_version.sql");
}

namespace {
std::string AvatarFilePath(const std::string& avatarDir, int64_t userId) {
    return avatarDir + "/" + std::to_string(userId) + ".bin";
}
}

int64_t UserRepository::CreateUser(const std::string& username, const std::string& passwordHash, const std::string& passwordSalt) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement insertUser(m_db, m_sqlCreateUser);
        insertUser.bind(1, username);
        insertUser.bind(2, passwordHash);
        insertUser.bind(3, passwordSalt);
        insertUser.exec();
        int64_t userId = m_db.getLastInsertRowid();

        // Первый когда-либо зарегистрированный пользователь становится admin'ом —
        // иначе некому было бы выдавать роли остальным. Все следующие — guest.
        SQLite::Statement countQuery(m_db, m_sqlCountUsers);
        countQuery.executeStep();
        int64_t userCount = countQuery.getColumn(0).getInt64();
        int64_t roleId = (userCount == 1) ? kAdminRoleId : kGuestRoleId;

        SQLite::Statement assignRole(m_db, m_sqlAssignRole);
        assignRole.bind(1, userId);
        assignRole.bind(2, roleId);
        assignRole.exec();

        if (userCount == 1) {
            // Первый пользователь дополнительно получает owner — не новые права
            // (admin уже даёт все биты), а неприкосновенность (см. Permissions.h).
            SQLite::Statement assignOwner(m_db, m_sqlAssignRole);
            assignOwner.bind(1, userId);
            assignOwner.bind(2, kOwnerRoleId);
            assignOwner.exec();
        }

        return userId;
    }
    catch (const SQLite::Exception&) {
        // UNIQUE constraint failed — username уже занят
        return -1;
    }
}

std::optional<UserRecord> UserRepository::FindByUsername(const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlFindByUsername);
    query.bind(1, username);

    if (query.executeStep()) {
        UserRecord record;
        record.id = query.getColumn(0).getInt64();
        record.username = query.getColumn(1).getString();
        record.passwordHash = query.getColumn(2).getString();
        record.passwordSalt = query.getColumn(3).getString();
        return record;
    }
    return std::nullopt;
}

bool UserRepository::DeleteUser(int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlDeleteUser);
    query.bind(1, userId);
    return query.exec() > 0;
}

std::vector<RoleRecord> UserRepository::ListRoles() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<RoleRecord> result;
    SQLite::Statement query(m_db, m_sqlListRoles);
    while (query.executeStep()) {
        RoleRecord record;
        record.id = query.getColumn(0).getInt64();
        record.name = query.getColumn(1).getString();
        record.isSystem = query.getColumn(2).getInt() != 0;
        record.permissions = static_cast<uint32_t>(query.getColumn(3).getInt64());
        record.displayName = query.getColumn(4).getString();
        result.push_back(record);
    }
    return result;
}

int64_t UserRepository::CreateRole(const std::string& name, uint32_t permissions, const std::string& displayName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        SQLite::Statement query(m_db, m_sqlCreateRole);
        query.bind(1, name);
        query.bind(2, static_cast<int64_t>(permissions));
        query.bind(3, displayName.empty() ? name : displayName);
        query.exec();
        return m_db.getLastInsertRowid();
    }
    catch (const SQLite::Exception&) {
        // UNIQUE constraint failed — имя занято
        return -1;
    }
}

RoleOpResult UserRepository::UpdateRole(int64_t roleId, const std::string& name, uint32_t permissions, const std::string& displayName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement findQuery(m_db, m_sqlFindRoleById);
    findQuery.bind(1, roleId);
    if (!findQuery.executeStep()) return RoleOpResult::NotFound;

    bool isSystem = findQuery.getColumn(2).getInt() != 0;
    std::string currentName = findQuery.getColumn(1).getString();

    if (isSystem) {
        // Системные роли нельзя переименовывать; admin и owner вообще нельзя
        // редактировать — их сила не хранится маской permissions (см. Permissions.h),
        // и правки туда ни на что не повлияют, но при этом обманчиво выглядели бы
        // применёнными. displayName — чисто косметическое поле, но эту же блокировку
        // применяем и к нему, чтобы не плодить отдельное исключение из общего правила.
        if (roleId == kAdminRoleId || roleId == kOwnerRoleId || name != currentName) {
            return RoleOpResult::SystemRole;
        }
    }

    try {
        SQLite::Statement updateQuery(m_db, m_sqlUpdateRole);
        updateQuery.bind(1, name);
        updateQuery.bind(2, static_cast<int64_t>(permissions));
        updateQuery.bind(3, displayName.empty() ? name : displayName);
        updateQuery.bind(4, roleId);
        updateQuery.exec();
        return RoleOpResult::Ok;
    }
    catch (const SQLite::Exception&) {
        return RoleOpResult::NameTaken;
    }
}

RoleOpResult UserRepository::DeleteRole(int64_t roleId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement findQuery(m_db, m_sqlFindRoleById);
    findQuery.bind(1, roleId);
    if (!findQuery.executeStep()) return RoleOpResult::NotFound;

    bool isSystem = findQuery.getColumn(2).getInt() != 0;
    if (isSystem) return RoleOpResult::SystemRole;

    SQLite::Statement deleteQuery(m_db, m_sqlDeleteRole);
    deleteQuery.bind(1, roleId);
    deleteQuery.exec();
    return RoleOpResult::Ok;
}

bool UserRepository::AssignRole(int64_t userId, int64_t roleId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlAssignRole);
    query.bind(1, userId);
    query.bind(2, roleId);
    return query.exec() > 0;
}

bool UserRepository::RemoveRole(int64_t userId, int64_t roleId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlRemoveRole);
    query.bind(1, userId);
    query.bind(2, roleId);
    return query.exec() > 0;
}

UserRoleData UserRepository::GetUserRoleData(int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    UserRoleData result;
    bool isAdmin = false;

    SQLite::Statement query(m_db, m_sqlGetUserRolePermissions);
    query.bind(1, userId);
    while (query.executeStep()) {
        int64_t roleId = query.getColumn(0).getInt64();
        result.roleIds.push_back(roleId);
        if (roleId == kAdminRoleId) {
            isAdmin = true; // суперпользователь — все права, включая будущие; см. Permissions.h
        }
        else {
            result.permissions |= static_cast<uint32_t>(query.getColumn(1).getInt64());
        }
    }
    if (isAdmin) result.permissions = 0xFFFFFFFFu;
    return result;
}

std::vector<UserSummary> UserRepository::ListUsers() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<UserSummary> result;

    SQLite::Statement query(m_db, m_sqlListUsers);
    while (query.executeStep()) {
        UserSummary user;
        user.id = query.getColumn(0).getInt64();
        user.username = query.getColumn(1).getString();
        user.avatarVersion = query.getColumn(2).getInt64();
        result.push_back(std::move(user));
    }

    // Роли — отдельным проходом по тому же запросу, что и GetUserRoleData, но без
    // подсчёта эффективных прав (панели участников нужны только сами roleIds).
    SQLite::Statement roleQuery(m_db, m_sqlGetUserRolePermissions);
    for (auto& user : result) {
        roleQuery.reset();
        roleQuery.bind(1, user.id);
        while (roleQuery.executeStep()) {
            user.roleIds.push_back(roleQuery.getColumn(0).getInt64());
        }
    }

    return result;
}

void UserRepository::BanIp(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlBanIp);
    query.bind(1, ip);
    query.exec();
}

void UserRepository::UnbanIp(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlUnbanIp);
    query.bind(1, ip);
    query.exec();
}

bool UserRepository::IsIpBanned(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SQLite::Statement query(m_db, m_sqlIsIpBanned);
    query.bind(1, ip);
    return query.executeStep();
}

std::vector<std::string> UserRepository::ListBannedIps() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    SQLite::Statement query(m_db, m_sqlListBannedIps);
    while (query.executeStep()) {
        result.push_back(query.getColumn(0).getString());
    }
    return result;
}

int64_t UserRepository::SetAvatar(int64_t userId, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ofstream file(AvatarFilePath(m_avatarDir, userId), std::ios::binary | std::ios::trunc);
    if (!file) return -1;
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    file.close();

    SQLite::Statement bump(m_db, m_sqlBumpAvatarVersion);
    bump.bind(1, userId);
    if (bump.exec() == 0) return -1;  // пользователь не найден

    SQLite::Statement readBack(m_db, m_sqlGetAvatarVersion);
    readBack.bind(1, userId);
    readBack.executeStep();
    return readBack.getColumn(0).getInt64();
}

AvatarData UserRepository::GetAvatar(int64_t userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    AvatarData result;

    SQLite::Statement query(m_db, m_sqlGetAvatarVersion);
    query.bind(1, userId);
    if (!query.executeStep()) return result;  // пользователь не найден
    result.version = query.getColumn(0).getInt64();
    if (result.version == 0) return result;  // версия есть, аватарки — нет

    std::ifstream file(AvatarFilePath(m_avatarDir, userId), std::ios::binary);
    if (!file) return result;  // версия ненулевая, а файла почему-то нет — отдаём как "нет аватарки"
    result.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return result;
}
