#pragma once
#include <string>
#include <vector>
#include <SQLiteCpp/SQLiteCpp.h>

struct Migration {
    int version = 0;
    std::string description;
    std::string sql;
};

// Сканирует папку на файлы вида "NNN_описание.sql", сортирует по номеру.
// Бросает std::runtime_error, если папки нет, она пуста, имя файла не разбирается
// или номера версий повторяются.
std::vector<Migration> LoadMigrationsFromDirectory(const std::string& dir);

// Применяет по порядку все миграции с номером больше текущего PRAGMA user_version.
// Каждая миграция — в своей транзакции (SQL из файла + обновление версии); при
// ошибке — откат и std::runtime_error с номером и описанием упавшей миграции.
void ApplyMigrations(SQLite::Database& db, const std::vector<Migration>& migrations);
