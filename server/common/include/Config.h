#pragma once
#include <cstdint>
#include <string>

// Общий config.json лежит рядом с исполняемыми файлами (в рабочей директории
// процесса — там же, где сейчас создаются *.db). Каждый сервис читает свою
// секцию; gateway дополнительно читает секции остальных, чтобы знать их порты.
//
// Файл обязателен: если его нет или он битый — это ошибка деплоя,
// и сервис должен громко упасть, а не тихо работать на выдуманных дефолтах.
struct AuthConfig {
    int port = 0;
    std::string dbPath;
    std::string avatarDir;  // файлы аватарок пользователей, рядом с dbPath
};

struct RoomConfig {
    int port = 0;
    std::string dbPath;
};

struct MessageConfig {
    int port = 0;
    std::string dbPath;
    int maxTextLength = 0;
};

struct TlsConfig {
    std::string certPath;
    std::string keyPath;
};

// Простой token bucket на сессию: capacity — сколько можно накопить про запас
// (разрешает короткий всплеск, например вставку нескольких строк подряд),
// perSecond — скорость восполнения. См. TryConsumeRateLimitToken в gateway/main.cpp.
struct RateLimitConfig {
    double messagesPerSecond = 0;
    double messageBurst = 0;
    double typingPerSecond = 0;
    double typingBurst = 0;
};

struct GatewayConfig {
    int port = 0;
    std::string serviceHost;
    int recvTimeoutMs = 0;
    int clientIdleTimeoutSec = 0;
    int serviceCallTimeoutMs = 0;
    TlsConfig tls;
    RateLimitConfig rateLimit;
    int avatarMaxSizeBytes = 0;  // общий лимит и для аватарок пользователей, и для иконки сервера
    std::string serverIconPath;  // один файл на весь деплой, рядом с gateway.crt/.key — своей БД у gateway нет
};

struct AppConfig {
    AuthConfig auth;
    RoomConfig room;
    MessageConfig message;
    GatewayConfig gateway;
};

// Бросает std::runtime_error с понятным сообщением, если файл не найден,
// не парсится как JSON или в нём не хватает обязательных полей.
AppConfig LoadConfig(const std::string& path = "config.json");
