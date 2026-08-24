#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Serialization.h"

// Клиент -> Gateway: отправить сообщение в комнату.
// senderId не передаётся — gateway берёт его из сессии, клиент не может выдать себя за другого.
struct ClientTextMessagePayload {
    int64_t roomId = 0;
    std::string text;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteString(buffer, text);
        return buffer;
    }
    static ClientTextMessagePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        ClientTextMessagePayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.text = ReadString(buffer, offset);
        return r;
    }
};

// Gateway -> Клиент: доставленное сообщение (тот же тип уходит и отправителю).
struct BroadcastTextMessagePayload {
    int64_t messageId = 0;
    int64_t roomId = 0;
    int64_t senderId = 0;
    std::string senderName;
    int64_t timestamp = 0;
    std::string text;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, messageId);
        WriteScalar(buffer, roomId);
        WriteScalar(buffer, senderId);
        WriteString(buffer, senderName);
        WriteScalar(buffer, timestamp);
        WriteString(buffer, text);
        return buffer;
    }
    static BroadcastTextMessagePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        BroadcastTextMessagePayload r;
        r.messageId = ReadScalar<int64_t>(buffer, offset);
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.senderId = ReadScalar<int64_t>(buffer, offset);
        r.senderName = ReadString(buffer, offset);
        r.timestamp = ReadScalar<int64_t>(buffer, offset);
        r.text = ReadString(buffer, offset);
        return r;
    }
};

// Gateway -> клиенты комнаты: кто-то вошёл или вышел
struct UserPresencePayload {
    int64_t roomId = 0;
    int64_t userId = 0;
    std::string username;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteScalar(buffer, userId);
        WriteString(buffer, username);
        return buffer;
    }
    static UserPresencePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        UserPresencePayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.userId = ReadScalar<int64_t>(buffer, offset);
        r.username = ReadString(buffer, offset);
        return r;
    }
};