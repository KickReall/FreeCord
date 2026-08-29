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

    RoomCreated = 0x0040,  // gateway -> все клиенты: появилась новая комната
    JoinRoom = 0x0010,  // клиент -> gateway: roomId
    JoinRoomResponse = 0x0011,  // gateway -> клиент: статус + список участников
    LeaveRoom = 0x0012,  // клиент -> gateway: roomId

    TextMessage = 0x0020,  // в обе стороны: roomId, senderId, timestamp, текст

    UserRegistered = 0x0041,  // gateway -> все: зарегистрировался новый пользователь
    UserJoined = 0x0030,  // gateway -> клиент: кто-то зашёл в комнату
    UserLeft = 0x0031,  // gateway -> клиент: кто-то вышел из комнаты
    ChannelKicked = 0x0032,  // gateway -> клиент: тебя кикнули из канала (roomId) — принудительный выход прямо сейчас

    // Забанить IP текущей активной сессии пользователя (панель участников, действие
    // "Заблокировать") — composite-обработчик, только gateway; не форвардится как есть
    // в auth, т.к. клиентский payload (userId) не совпадает с внутренним (ip).
    BanUserSessionRequest = 0x0050,  // userId
    BanUserSessionResponse = 0x0051,  // status: 0 = ok, 1 = не в сети, 254 = нет прав

    Ping = 0x00F0,
    Pong = 0x00F1,
    Error = 0x00FF,  // code + сообщение об ошибке

    // --- Gateway <-> Auth (internal) ---
    AuthenticateRequest = 0x1000,  // gateway -> auth: проверить логин/пароль
    AuthenticateResponse = 0x1001,  // auth -> gateway: статус + userId + sessionId
    IsIpBannedRequest = 0x1002,  // internal only, gateway -> auth: ip — проверка перед TLS-хендшейком
    IsIpBannedResponse = 0x1003,  // internal only, auth -> gateway: banned

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
    ChannelOverridesRequest = 0x200A,  // gateway -> room: roomId
    ChannelOverridesResponse = 0x200B,  // room -> gateway: список оверрайдов (roleId, allow, deny)
    SetChannelOverrideRequest = 0x200C,  // gateway -> room: roomId, roleId, allow, deny
    SetChannelOverrideResponse = 0x200D,  // room -> gateway: status
    DeleteChannelOverrideRequest = 0x200E,  // gateway -> room: roomId, roleId — сброс к базовым правам роли
    DeleteChannelOverrideResponse = 0x200F,  // room -> gateway: status

    // Модерация по каналам: бан (используется и для кика — gateway дополнительно
    // принудительно выкидывает пользователя, если он сейчас онлайн в этой комнате) и мут.
    // Те же типы используются и клиент->gateway, и gateway->room, как и остальные Room-сообщения.
    ChannelModerationStatusRequest = 0x2010,  // gateway -> room: roomId, userId (internal only)
    ChannelModerationStatusResponse = 0x2011,  // room -> gateway: banned + muted (internal only)
    ChannelKickRequest = 0x2012,  // roomId, userId — бан от канала
    ChannelKickResponse = 0x2013,  // status
    ChannelUnbanRequest = 0x2014,  // roomId, userId
    ChannelUnbanResponse = 0x2015,  // status
    ChannelMuteRequest = 0x2016,  // roomId, userId
    ChannelMuteResponse = 0x2017,  // status
    ChannelUnmuteRequest = 0x2018,  // roomId, userId
    ChannelUnmuteResponse = 0x2019,  // status

    // --- Gateway <-> Message (internal) ---
    SendMessageRequest = 0x3000,  // gateway -> message: roomId, senderId, text
    SendMessageResponse = 0x3001,  // message -> gateway: status + messageId + timestamp
    HistoryRequest = 0x3002,  // gateway -> message: roomId, limit
    HistoryResponse = 0x3003,  // message -> gateway: список сообщений

    // --- Роли: те же значения используются и клиент->gateway, и gateway->auth
    // (как для Room/Message) — gateway просто форвардит payload как есть ---
    RoleListRequest = 0x4000,  // (пусто) — все роли
    RoleListResponse = 0x4001,
    RoleCreateRequest = 0x4002,  // name, permissions
    RoleCreateResponse = 0x4003,  // status + roleId
    RoleUpdateRequest = 0x4004,  // roleId, name, permissions
    RoleUpdateResponse = 0x4005,  // status
    RoleDeleteRequest = 0x4006,  // roleId
    RoleDeleteResponse = 0x4007,  // status
    RoleAssignRequest = 0x4008,  // userId, roleId
    RoleAssignResponse = 0x4009,  // status
    RoleRemoveRequest = 0x400A,  // userId, roleId
    RoleRemoveResponse = 0x400B,  // status
    GetUserPermissionsRequest = 0x400C,  // internal only, gateway -> auth: userId
    GetUserPermissionsResponse = 0x400D,  // internal only, auth -> gateway: permissions
    MyPermissions = 0x400E,  // gateway -> клиент, отправляется сразу после успешного логина

    // Список всех пользователей с их ролями — для панели участников на клиенте.
    // Видно любому залогиненному (как и RoleListRequest), то же значение клиент->gateway
    // и gateway->auth (raw-forward).
    UserListRequest = 0x400F,  // (пусто)
    UserListResponse = 0x4010,

    // --- Бан по IP на уровне всего сервера: те же типы клиент->gateway и gateway->auth ---
    IpBanListRequest = 0x5000,  // (пусто) — список забаненных IP
    IpBanListResponse = 0x5001,
    IpBanRequest = 0x5002,  // ip
    IpBanResponse = 0x5003,  // status
    IpUnbanRequest = 0x5004,  // ip
    IpUnbanResponse = 0x5005,  // status
};