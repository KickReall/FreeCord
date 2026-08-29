#include "UserRepository.h"
#include "MigrationRunner.h"
#include "SqlFile.h"
#include "Permissions.h"

UserRepository::UserRepository(const std::string& dbPath)
    : m_db(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    m_db.exec("PRAGMA foreign_keys = ON");

    ApplyMigrations(m_db, LoadMigrationsFromDirectory("db/auth/migrations"));

    m_sqlCreateUser = LoadSqlFile("db/auth/queries/create_user.sql");
    m_sqlFindByUsername = LoadSqlFile("db/auth/queries/find_by_username.sql");
    m_sqlCountUsers = LoadSqlFile("db/auth/queries/count_users.sql");
    m_sqlAssignRole = LoadSqlFile("db/auth/queries/assign_role.sql");
    m_sqlListRoles = LoadSqlFile("db/auth/queries/list_roles.sql");
    m_sqlCreateRole = LoadSqlFile("db/auth/queries/create_role.sql");
    m_sqlFindRoleById = LoadSqlFile("db/auth/queries/find_role_by_id.sql");
    m_sqlUpdateRole = LoadSqlFile("db/auth/queries/update_role.sql");
    m_sqlDeleteRole = LoadSqlFile("db/auth/queries/delete_role.sql");
    m_sqlRemoveRole = LoadSqlFile("db/auth/queries/remove_role.sql");
    m_sqlGetUserRolePermissions = LoadSqlFile("db/auth/queries/get_user_role_permissions.sql");
}

int64_t UserRepository::CreateUser(const std::string& username, const std::string& passwordHash, const std::string& passwordSalt) {
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

        return userId;
    }
    catch (const SQLite::Exception&) {
        // UNIQUE constraint failed — username уже занят
        return -1;
    }
}

std::optional<UserRecord> UserRepository::FindByUsername(const std::string& username) {
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

std::vector<RoleRecord> UserRepository::ListRoles() {
    std::vector<RoleRecord> result;
    SQLite::Statement query(m_db, m_sqlListRoles);
    while (query.executeStep()) {
        RoleRecord record;
        record.id = query.getColumn(0).getInt64();
        record.name = query.getColumn(1).getString();
        record.isSystem = query.getColumn(2).getInt() != 0;
        record.permissions = static_cast<uint32_t>(query.getColumn(3).getInt64());
        result.push_back(record);
    }
    return result;
}

int64_t UserRepository::CreateRole(const std::string& name, uint32_t permissions) {
    try {
        SQLite::Statement query(m_db, m_sqlCreateRole);
        query.bind(1, name);
        query.bind(2, static_cast<int64_t>(permissions));
        query.exec();
        return m_db.getLastInsertRowid();
    }
    catch (const SQLite::Exception&) {
        // UNIQUE constraint failed — имя занято
        return -1;
    }
}

RoleOpResult UserRepository::UpdateRole(int64_t roleId, const std::string& name, uint32_t permissions) {
    SQLite::Statement findQuery(m_db, m_sqlFindRoleById);
    findQuery.bind(1, roleId);
    if (!findQuery.executeStep()) return RoleOpResult::NotFound;

    bool isSystem = findQuery.getColumn(2).getInt() != 0;
    std::string currentName = findQuery.getColumn(1).getString();

    if (isSystem) {
        // Системные роли нельзя переименовывать; admin вообще нельзя редактировать —
        // его права не хранятся маской и правки ни на что не повлияют (см. Permissions.h).
        if (roleId == kAdminRoleId || name != currentName) {
            return RoleOpResult::SystemRole;
        }
    }

    try {
        SQLite::Statement updateQuery(m_db, m_sqlUpdateRole);
        updateQuery.bind(1, name);
        updateQuery.bind(2, static_cast<int64_t>(permissions));
        updateQuery.bind(3, roleId);
        updateQuery.exec();
        return RoleOpResult::Ok;
    }
    catch (const SQLite::Exception&) {
        return RoleOpResult::NameTaken;
    }
}

RoleOpResult UserRepository::DeleteRole(int64_t roleId) {
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
    SQLite::Statement query(m_db, m_sqlAssignRole);
    query.bind(1, userId);
    query.bind(2, roleId);
    return query.exec() > 0;
}

bool UserRepository::RemoveRole(int64_t userId, int64_t roleId) {
    SQLite::Statement query(m_db, m_sqlRemoveRole);
    query.bind(1, userId);
    query.bind(2, roleId);
    return query.exec() > 0;
}

uint32_t UserRepository::GetUserPermissions(int64_t userId) {
    SQLite::Statement query(m_db, m_sqlGetUserRolePermissions);
    query.bind(1, userId);

    uint32_t combined = 0;
    while (query.executeStep()) {
        int64_t roleId = query.getColumn(0).getInt64();
        if (roleId == kAdminRoleId) return 0xFFFFFFFFu; // суперпользователь — все права, включая будущие
        combined |= static_cast<uint32_t>(query.getColumn(1).getInt64());
    }
    return combined;
}
