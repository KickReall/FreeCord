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
}