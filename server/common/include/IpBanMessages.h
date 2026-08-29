#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Serialization.h"

// Общий payload для бана/разбана/проверки — везде нужен только сам IP.
// Используется и клиент->gateway->auth (Ban/Unban), и gateway->auth internal (проверка).
struct IpPayload {
    std::string ip;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteString(buffer, ip);
        return buffer;
    }
    static IpPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        IpPayload r;
        r.ip = ReadString(buffer, offset);
        return r;
    }
};

// Ответ на IsIpBannedRequest — internal only, gateway -> auth, никогда не идёт клиенту.
struct IpBanStatusPayload {
    uint8_t banned = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, banned);
        return buffer;
    }
    static IpBanStatusPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        IpBanStatusPayload r;
        r.banned = ReadScalar<uint8_t>(buffer, offset);
        return r;
    }
};

struct IpBanListResponsePayload {
    std::vector<std::string> ips;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, static_cast<uint32_t>(ips.size()));
        for (const auto& ip : ips) WriteString(buffer, ip);
        return buffer;
    }
    static IpBanListResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        IpBanListResponsePayload r;
        uint32_t count = ReadScalar<uint32_t>(buffer, offset);
        for (uint32_t i = 0; i < count; ++i) r.ips.push_back(ReadString(buffer, offset));
        return r;
    }
};
