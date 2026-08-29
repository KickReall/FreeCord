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
	TypingRequest = 0x0021,  // клиент -> gateway: roomId — "я печатаю"
	TypingBroadcast = 0x0022,  // gateway -> остальным в комнате: roomId, senderId, senderName

    UserRegistered = 0x0041,
    UserJoined = 0x0030,
	UserLeft = 0x0031,
	ChannelKicked = 0x0032,

	// Панель участников, действие "Заблокировать" — банит IP текущей сессии пользователя
	BanUserSessionRequest = 0x0050,
	BanUserSessionResponse = 0x0051,

	// Панель участников, действие "Удалить" — сносит аккаунт целиком (необратимо)
	DeleteUserRequest = 0x0052,
	DeleteUserResponse = 0x0053,

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
	ChannelOverridesRequest = 0x200A,
	ChannelOverridesResponse = 0x200B,
	SetChannelOverrideRequest = 0x200C,
	SetChannelOverrideResponse = 0x200D,
	DeleteChannelOverrideRequest = 0x200E,
	DeleteChannelOverrideResponse = 0x200F,

	// Модерация по каналам: кик = бан + принудительный выход онлайн-пользователя
	ChannelKickRequest = 0x2012,
	ChannelKickResponse = 0x2013,
	ChannelUnbanRequest = 0x2014,
	ChannelUnbanResponse = 0x2015,
	ChannelMuteRequest = 0x2016,
	ChannelMuteResponse = 0x2017,
	ChannelUnmuteRequest = 0x2018,
	ChannelUnmuteResponse = 0x2019,

	// Бан по IP на уровне всего сервера
	IpBanListRequest = 0x5000,
	IpBanListResponse = 0x5001,
	IpBanRequest = 0x5002,
	IpBanResponse = 0x5003,
	IpUnbanRequest = 0x5004,
	IpUnbanResponse = 0x5005,

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

	// Список пользователей с их ролями — для панели участников
	UserListRequest = 0x400F,
	UserListResponse = 0x4010,
}