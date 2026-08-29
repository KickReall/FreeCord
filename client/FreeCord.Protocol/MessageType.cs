namespace FreeCord.Protocol;

public enum MessageType : ushort
{
	// Client <-> Gateway
	AuthRequest = 0x0001,
	AuthResponse = 0x0002,
	RegisterRequest = 0x0003,
	RegisterResponse = 0x0004,

	JoinRoom = 0x0010,
	JoinRoomResponse = 0x0011,
	LeaveRoom = 0x0012,

	TextMessage = 0x0020,

    UserRegistered = 0x0041,
    UserJoined = 0x0030,
	UserLeft = 0x0031,

	Ping = 0x00F0,
	Pong = 0x00F1,
	Error = 0x00FF,

    // Проксируются gateway во внутренние сервисы
    RoomCreated = 0x0040,
    RoomCreateRequest = 0x2000,
	RoomCreateResponse = 0x2001,
	RoomLeaveResponse = 0x2005,
	RoomListRequest = 0x2006,
	RoomListResponse = 0x2007,

	HistoryRequest = 0x3002,
	HistoryResponse = 0x3003,

	// Роли: то же значение используется и клиент->gateway, и gateway->auth
	RoleListRequest = 0x4000,
	RoleListResponse = 0x4001,
	RoleCreateRequest = 0x4002,
	RoleCreateResponse = 0x4003,
	RoleUpdateRequest = 0x4004,
	RoleUpdateResponse = 0x4005,
	RoleDeleteRequest = 0x4006,
	RoleDeleteResponse = 0x4007,
	RoleAssignRequest = 0x4008,
	RoleAssignResponse = 0x4009,
	RoleRemoveRequest = 0x400A,
	RoleRemoveResponse = 0x400B,
	MyPermissions = 0x400E,  // gateway -> клиент, сразу после успешного логина
}