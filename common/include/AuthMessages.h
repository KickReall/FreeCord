#pragma once
#include <cstdint>
#include <string>
#include <vector>

// --- Сериализация строк: uint16_t длина (в байтах, UTF-8) + сами байты, без null-terminator ---

inline void WriteString(std::vector<uint8_t>& buffer, const std::string& str) {
    uint16_t len = static_cast<uint16_t>(str.size());
    const uint8_t* lenBytes = reinterpret_cast<const uint8_t*>(&len);
    buffer.insert(buffer.end(), lenBytes, lenBytes + sizeof(len));
    buffer.insert(buffer.end(), str.begin(), str.end());
}

// offset передаётся по ссылке и сдвигается сам — удобно читать несколько полей подряд
inline std::string ReadString(const std::vector<uint8_t>& buffer, size_t& offset) {
    uint16_t len = 0;
    std::memcpy(&len, buffer.data() + offset, sizeof(len));
    offset += sizeof(len);

    std::string result(reinterpret_cast<const char*>(buffer.data() + offset), len);
    offset += len;
    return result;
}

struct AuthRequestPayload {
    std::string username;
    std::string password;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteString(buffer, username);
        WriteString(buffer, password);
        return buffer;
    }

    static AuthRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        AuthRequestPayload result;
        result.username = ReadString(buffer, offset);
        result.password = ReadString(buffer, offset);
        return result;
    }
};

struct AuthResponsePayload {
    uint8_t status = 0;      // 0 = success, 1 = invalid credentials, ...
    uint64_t sessionId = 0;
    uint32_t userId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        buffer.push_back(status);
        const uint8_t* sidBytes = reinterpret_cast<const uint8_t*>(&sessionId);
        buffer.insert(buffer.end(), sidBytes, sidBytes + sizeof(sessionId));
        const uint8_t* uidBytes = reinterpret_cast<const uint8_t*>(&userId);
        buffer.insert(buffer.end(), uidBytes, uidBytes + sizeof(userId));
        return buffer;
    }

    static AuthResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        AuthResponsePayload result;
        result.status = buffer[offset]; offset += 1;
        std::memcpy(&result.sessionId, buffer.data() + offset, sizeof(result.sessionId)); offset += sizeof(result.sessionId);
        std::memcpy(&result.userId, buffer.data() + offset, sizeof(result.userId)); offset += sizeof(result.userId);
        return result;
    }
};