#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct ControlHeader {
    uint32_t length;       // длина payload В БАЙТАХ, без учёта самого заголовка
    uint16_t messageType;
    uint32_t sequence;     // для сопоставления запрос/ответ, пока не обязателен к использованию
};
#pragma pack(pop)

constexpr size_t CONTROL_HEADER_SIZE = sizeof(ControlHeader);

// Диапазоны messageType по слоям, чтобы легко отличить, откуда пришло сообщение:
//   0x0000–0x0FFF — Client <-> Gateway
//   0x1000–0x1FFF — Gateway <-> Auth (internal)
//   0x2000–0x2FFF — Gateway <-> Room (internal)
//   0x3000–0x3FFF — Gateway <-> Message (internal)
enum class MessageType : uint16_t {
    // --- Client <-> Gateway ---
    RegisterRequest = 0x0003,  // клиент -> gateway -> auth: username + password
    RegisterResponse = 0x0004,  // ответный статус + userId
    AuthRequest = 0x0001,  // клиент -> gateway: логин + пароль
    AuthResponse = 0x0002,  // gateway -> клиент: статус + sessionId + userId

    JoinRoom = 0x0010,  // клиент -> gateway: roomId
    JoinRoomResponse = 0x0011,  // gateway -> клиент: статус + список участников
    LeaveRoom = 0x0012,  // клиент -> gateway: roomId

    TextMessage = 0x0020,  // в обе стороны: roomId, senderId, timestamp, текст

    UserJoined = 0x0030,  // gateway -> клиент: кто-то зашёл в комнату
    UserLeft = 0x0031,  // gateway -> клиент: кто-то вышел из комнаты

    Ping = 0x00F0,
    Pong = 0x00F1,
    Error = 0x00FF,  // code + сообщение об ошибке

    // --- Gateway <-> Auth (internal) ---
    AuthenticateRequest = 0x1000,  // gateway -> auth: проверить логин/пароль
    AuthenticateResponse = 0x1001,  // auth -> gateway: статус + userId + sessionId

    // --- Gateway <-> Room (internal) ---
    RoomCreateRequest = 0x2000,  // gateway -> room: name
    RoomCreateResponse = 0x2001,  // room -> gateway: status + roomId
    RoomJoinRequest = 0x2002,  // gateway -> room: roomId + userId
    RoomJoinResponse = 0x2003,  // room -> gateway: status
    RoomLeaveRequest = 0x2004,  // gateway -> room: roomId + userId
    RoomLeaveResponse = 0x2005,  // room -> gateway: status
    RoomListRequest = 0x2006,  // gateway -> room: (пусто) — все комнаты
    RoomListResponse = 0x2007,  // room -> gateway: список комнат
    RoomMembersRequest = 0x2008,  // gateway -> room: roomId
    RoomMembersResponse = 0x2009,  // room -> gateway: список userId участников

    // --- Gateway <-> Message (internal) ---
    SendMessageRequest = 0x3000,  // gateway -> message: сохранить + разослать
    SendMessageResponse = 0x3001,  // message -> gateway: статус + messageId
    FanoutMessage = 0x3002,  // message -> gateway: разослать всем в комнате
};