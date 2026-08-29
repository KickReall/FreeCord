#pragma once
#include <string>

namespace PasswordHasher {
    // Генерирует случайную соль (16 байт, в hex-строке — 32 символа).
    std::string GenerateSalt();

    // PBKDF2-HMAC-SHA256, 100000 итераций. Возвращает hex-строку хеша.
    std::string HashPassword(const std::string& password, const std::string& saltHex);

    // Сравнивает пароль с ранее сохранённым хешем/солью.
    bool VerifyPassword(const std::string& password, const std::string& saltHex, const std::string& expectedHashHex);
}