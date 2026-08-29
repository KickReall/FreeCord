#pragma once
#include <cstdint>
#include <string>

// Общий config.json лежит рядом с исполняемыми файлами (в рабочей директории
// процесса — там же, где сейчас создаются *.db). Каждый сервис читает свою
// секцию; gateway дополнительно читает секции остальных, чтобы знать их порты.
//
// Файл обязателен: если его нет или он битый — это ошибка деплоя,
// и сервис должен громко упасть, а не тихо работать на выдуманных дефолтах.
struct ServiceEndpointConfig {
    int port = 0;
    std::string dbPath;
};

struct GatewayConfig {
    int port = 0;
    std::string serviceHost;
};

struct AppConfig {
    ServiceEndpointConfig auth;
    ServiceEndpointConfig room;
    ServiceEndpointConfig message;
    GatewayConfig gateway;
};

// Бросает std::runtime_error с понятным сообщением, если файл не найден,
// не парсится как JSON или в нём не хватает обязательных полей.
AppConfig LoadConfig(const std::string& path = "config.json");
