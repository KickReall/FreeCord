#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Serialization.h"

struct SendMessageRequestPayload {
    int64_t roomId = 0;
    int64_t senderId = 0;
    std::string text;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteScalar(buffer, senderId);
        WriteString(buffer, text);
        return buffer;
    }
    static SendMessageRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        SendMessageRequestPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.senderId = ReadScalar<int64_t>(buffer, offset);
        r.text = ReadString(buffer, offset);
        return r;
    }
};

struct SendMessageResponsePayload {
    uint8_t status = 0;      // 0 = ok, 1 = empty text, 2 = text too long
    int64_t messageId = 0;
    int64_t timestamp = 0;   // unix time, секунды

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, status);
        WriteScalar(buffer, messageId);
        WriteScalar(buffer, timestamp);
        return buffer;
    }
    static SendMessageResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        SendMessageResponsePayload r;
        r.status = ReadScalar<uint8_t>(buffer, offset);
        r.messageId = ReadScalar<int64_t>(buffer, offset);
        r.timestamp = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

struct HistoryRequestPayload {
    int64_t roomId = 0;
    uint32_t limit = 50;     // сколько последних сообщений вернуть

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteScalar(buffer, limit);
        return buffer;
    }
    static HistoryRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        HistoryRequestPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.limit = ReadScalar<uint32_t>(buffer, offset);
        return r;
    }
};

struct ChatMessage {
    int64_t id = 0;
    int64_t roomId = 0;
    int64_t senderId = 0;
    int64_t timestamp = 0;
    std::string text;
};

struct HistoryResponsePayload {
    std::vector<ChatMessage> messages;   // от старых к новым

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, static_cast<uint32_t>(messages.size()));
        for (const auto& msg : messages) {
            WriteScalar(buffer, msg.id);
            WriteScalar(buffer, msg.roomId);
            WriteScalar(buffer, msg.senderId);
            WriteScalar(buffer, msg.timestamp);
            WriteString(buffer, msg.text);
        }
        return buffer;
    }
    static HistoryResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        HistoryResponsePayload r;
        uint32_t count = ReadScalar<uint32_t>(buffer, offset);
        for (uint32_t i = 0; i < count; ++i) {
            ChatMessage msg;
            msg.id = ReadScalar<int64_t>(buffer, offset);
            msg.roomId = ReadScalar<int64_t>(buffer, offset);
            msg.senderId = ReadScalar<int64_t>(buffer, offset);
            msg.timestamp = ReadScalar<int64_t>(buffer, offset);
            msg.text = ReadString(buffer, offset);
            r.messages.push_back(msg);
        }
        return r;
    }
};