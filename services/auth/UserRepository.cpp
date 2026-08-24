#include "UserRepository.h"

UserRepository::UserRepository(const std::string& dbPath)
    : m_db(dbPath, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)
{
    m_db.exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            password_salt TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )");
}

int64_t UserRepository::CreateUser(const std::string& username, const std::string& passwordHash, const std::string& passwordSalt) {
    try {
        SQLite::Statement query(m_db, "INSERT INTO users (username, password_hash, password_salt) VALUES (?, ?, ?)");
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
    SQLite::Statement query(m_db, "SELECT id, username, password_hash, password_salt FROM users WHERE username = ?");
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