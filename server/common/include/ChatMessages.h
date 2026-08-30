#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Serialization.h"
#include "RoomMessages.h"

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

// Клиент -> Gateway: "я печатаю" в этой комнате. senderId/senderName в broadcast
// берутся из сессии (как и в ClientTextMessagePayload) — клиент их не присылает.
struct TypingRequestPayload {
    int64_t roomId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        return buffer;
    }
    static TypingRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        TypingRequestPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

// Gateway -> остальным участникам комнаты (кроме автора): кто-то печатает.
// Разовое уведомление без явного "перестал печатать" — клиент сам гасит индикатор
// по таймауту, если новое TypingBroadcast для этого senderId не пришло вовремя.
struct TypingBroadcastPayload {
    int64_t roomId = 0;
    int64_t senderId = 0;
    std::string senderName;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteScalar(buffer, senderId);
        WriteString(buffer, senderName);
        return buffer;
    }
    static TypingBroadcastPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        TypingBroadcastPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.senderId = ReadScalar<int64_t>(buffer, offset);
        r.senderName = ReadString(buffer, offset);
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

// Gateway -> все подключённые клиенты: создана новая комната
struct RoomCreatedPayload {
    int64_t roomId = 0;
    std::string name;
    RoomType type = RoomType::Text;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteString(buffer, name);
        WriteScalar(buffer, static_cast<uint8_t>(type));
        return buffer;
    }
    static RoomCreatedPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomCreatedPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.name = ReadString(buffer, offset);
        r.type = static_cast<RoomType>(ReadScalar<uint8_t>(buffer, offset));
        return r;
    }
};

// Gateway -> все подключённые клиенты: канал переименован. Досылается и самому
// инициатору — из одного статус-ответа на RoomUpdateRequest новое имя не узнать.
struct RoomUpdatedPayload {
    int64_t roomId = 0;
    std::string name;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        WriteString(buffer, name);
        return buffer;
    }
    static RoomUpdatedPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomUpdatedPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        r.name = ReadString(buffer, offset);
        return r;
    }
};

// Gateway -> все подключённые клиенты: канал удалён — убрать из своего списка.
// Тем, кто держал его открытым, отдельно прилетает ChannelKicked (см. ForceLeaveRoomForAll
// в gateway/main.cpp) до этого пуша, чтобы currentRoomId на сервере тоже сбросился.
struct RoomDeletedPayload {
    int64_t roomId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, roomId);
        return buffer;
    }
    static RoomDeletedPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        RoomDeletedPayload r;
        r.roomId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

// Gateway -> все клиенты: в системе появился новый пользователь
struct UserRegisteredPayload {
    int64_t userId = 0;
    std::string username;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, userId);
        WriteString(buffer, username);
        return buffer;
    }
    static UserRegisteredPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        UserRegisteredPayload r;
        r.userId = ReadScalar<int64_t>(buffer, offset);
        r.username = ReadString(buffer, offset);
        return r;
    }
};