#include "UserRepository.h"
#include "MigrationRunner.h"
#include "SqlFile.h"

UserRepository::UserRepository(const std::string& dbPath)
    : m_db(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    ApplyMigrations(m_db, LoadMigrationsFromDirectory("db/auth/migrations"));

    m_sqlCreateUser = LoadSqlFile("db/auth/queries/create_user.sql");
    m_sqlFindByUsername = LoadSqlFile("db/auth/queries/find_by_username.sql");
}

int64_t UserRepository::CreateUser(const std::string& username, const std::string& passwordHash, const std::string& passwordSalt) {
    try {
        SQLite::Statement query(m_db, m_sqlCreateUser);
        query.bind(1, username);
        query.bind(2, passwordHash);
        query.bind(3, passwordSalt);
        query.exec();
        return m_db.getLastInsertRowid();
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
