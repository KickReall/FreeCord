#include "Config.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace {

    using json = nlohmann::json;

    // Достаёт обязательное поле; бросает исключение с указанием секции/поля,
    // чтобы ошибку конфигурации было видно сразу, а не гадать по обрезанному JSON.
    template <typename T>
    T RequireField(const json& section, const std::string& sectionName, const std::string& field) {
        if (!section.contains(field)) {
            throw std::runtime_error("config.json: missing field \"" + field + "\" in section \"" + sectionName + "\"");
        }
        return section.at(field).get<T>();
    }

    const json& RequireSection(const json& root, const std::string& name) {
        if (!root.contains(name)) {
            throw std::runtime_error("config.json: missing section \"" + name + "\"");
        }
        return root.at(name);
    }

} // namespace

AppConfig LoadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path +
            " (expected in the current working directory)");
    }

    json root;
    try {
        file >> root;
    }
    catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to parse " + path + ": " + e.what());
    }

    AppConfig config;

    const json& authSection = RequireSection(root, "auth");
    config.auth.port = RequireField<int>(authSection, "auth", "port");
    config.auth.dbPath = RequireField<std::string>(authSection, "auth", "dbPath");

    const json& roomSection = RequireSection(root, "room");
    config.room.port = RequireField<int>(roomSection, "room", "port");
    config.room.dbPath = RequireField<std::string>(roomSection, "room", "dbPath");

    const json& messageSection = RequireSection(root, "message");
    config.message.port = RequireField<int>(messageSection, "message", "port");
    config.message.dbPath = RequireField<std::string>(messageSection, "message", "dbPath");
    config.message.maxTextLength = RequireField<int>(messageSection, "message", "maxTextLength");

    const json& gatewaySection = RequireSection(root, "gateway");
    config.gateway.port = RequireField<int>(gatewaySection, "gateway", "port");
    config.gateway.serviceHost = RequireField<std::string>(gatewaySection, "gateway", "serviceHost");
    config.gateway.recvTimeoutMs = RequireField<int>(gatewaySection, "gateway", "recvTimeoutMs");
    config.gateway.clientIdleTimeoutSec = RequireField<int>(gatewaySection, "gateway", "clientIdleTimeoutSec");
    config.gateway.serviceCallTimeoutMs = RequireField<int>(gatewaySection, "gateway", "serviceCallTimeoutMs");

    const json& tlsSection = RequireSection(gatewaySection, "tls");
    config.gateway.tls.certPath = RequireField<std::string>(tlsSection, "gateway.tls", "certPath");
    config.gateway.tls.keyPath = RequireField<std::string>(tlsSection, "gateway.tls", "keyPath");

    const json& rateLimitSection = RequireSection(gatewaySection, "rateLimit");
    config.gateway.rateLimit.messagesPerSecond = RequireField<double>(rateLimitSection, "gateway.rateLimit", "messagesPerSecond");
    config.gateway.rateLimit.messageBurst = RequireField<double>(rateLimitSection, "gateway.rateLimit", "messageBurst");
    config.gateway.rateLimit.typingPerSecond = RequireField<double>(rateLimitSection, "gateway.rateLimit", "typingPerSecond");
    config.gateway.rateLimit.typingBurst = RequireField<double>(rateLimitSection, "gateway.rateLimit", "typingBurst");

    return config;
}
