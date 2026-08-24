#pragma once
#include <string>
#include <optional>
#include <SQLiteCpp/SQLiteCpp.h>

struct UserRecord {
    int64_t id;
    std::string username;
    std::string passwordHash;
    std::string passwordSalt;
};

class UserRepository {
public:
    explicit UserRepository(const std::string& dbPath);

    // Возвращает id нового пользователя, либо -1, если username уже занят.
    int64_t CreateUser(const std::string& username, const std::string& passwordHash, const std::string& passwordSalt);

    std::optional<UserRecord> FindByUsername(const std::string& username);

private:
    SQLite::Database m_db;
};