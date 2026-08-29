#include "MigrationRunner.h"
#include "SqlFile.h"
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <cctype>

namespace fs = std::filesystem;

namespace {

    // "001_initial.sql" -> version=1, description="initial"
    bool ParseMigrationFilename(const std::string& filename, int& outVersion, std::string& outDescription) {
        auto underscorePos = filename.find('_');
        auto dotPos = filename.rfind('.');
        if (underscorePos == std::string::npos || dotPos == std::string::npos || dotPos <= underscorePos) {
            return false;
        }

        std::string versionPart = filename.substr(0, underscorePos);
        bool allDigits = !versionPart.empty() && std::all_of(versionPart.begin(), versionPart.end(),
            [](unsigned char c) { return std::isdigit(c); });
        if (!allDigits) {
            return false;
        }

        outVersion = std::stoi(versionPart);
        outDescription = filename.substr(underscorePos + 1, dotPos - underscorePos - 1);
        return true;
    }

} // namespace

std::vector<Migration> LoadMigrationsFromDirectory(const std::string& dir) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        throw std::runtime_error("Migrations directory not found: " + dir);
    }

    std::vector<Migration> migrations;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sql") continue;

        std::string filename = entry.path().filename().string();
        int version = 0;
        std::string description;
        if (!ParseMigrationFilename(filename, version, description)) {
            throw std::runtime_error("Migration filename doesn't match NNN_description.sql: " + filename);
        }

        Migration migration;
        migration.version = version;
        migration.description = description;
        migration.sql = LoadSqlFile(entry.path().string());
        migrations.push_back(std::move(migration));
    }

    if (migrations.empty()) {
        throw std::runtime_error("No migrations found in " + dir);
    }

    std::sort(migrations.begin(), migrations.end(),
        [](const Migration& a, const Migration& b) { return a.version < b.version; });

    for (size_t i = 1; i < migrations.size(); i++) {
        if (migrations[i].version == migrations[i - 1].version) {
            throw std::runtime_error("Duplicate migration version " + std::to_string(migrations[i].version) +
                " in " + dir);
        }
    }

    return migrations;
}

void ApplyMigrations(SQLite::Database& db, const std::vector<Migration>& migrations) {
    int currentVersion = db.execAndGet("PRAGMA user_version").getInt();

    for (const auto& migration : migrations) {
        if (migration.version <= currentVersion) continue;

        try {
            SQLite::Transaction transaction(db);
            db.exec(migration.sql);
            // PRAGMA не поддерживает bind-параметры, но версия — не пользовательский
            // ввод, а число из имени файла, так что конкатенация здесь безопасна.
            db.exec("PRAGMA user_version = " + std::to_string(migration.version));
            transaction.commit();
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Migration " + std::to_string(migration.version) + " (" +
                migration.description + ") failed: " + e.what());
        }
    }
}
