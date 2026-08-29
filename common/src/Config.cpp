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

    ServiceEndpointConfig ParseEndpoint(const json& root, const std::string& name) {
        const json& section = RequireSection(root, name);
        ServiceEndpointConfig config;
        config.port = RequireField<int>(section, name, "port");
        config.dbPath = RequireField<std::string>(section, name, "dbPath");
        return config;
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
    config.auth = ParseEndpoint(root, "auth");
    config.room = ParseEndpoint(root, "room");
    config.message = ParseEndpoint(root, "message");

    const json& gatewaySection = RequireSection(root, "gateway");
    config.gateway.port = RequireField<int>(gatewaySection, "gateway", "port");
    config.gateway.serviceHost = RequireField<std::string>(gatewaySection, "gateway", "serviceHost");

    return config;
}
