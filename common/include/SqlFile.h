#pragma once
#include <string>

// Читает файл целиком в строку. Бросает std::runtime_error с понятным сообщением,
// если файла нет или он пустой — тот же принцип, что и у LoadConfig: громкая
// ошибка при неполном деплое лучше тихой работы с пустым запросом.
std::string LoadSqlFile(const std::string& path);
