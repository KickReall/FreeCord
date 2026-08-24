#include <iostream>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "RoomMessages.h"
#include "MessageMessages.h"
#include "ChatMessages.h"
#include "ServiceClient.h"
#include "SessionManager.h"

constexpr int GATEWAY_PORT = 6000;
constexpr const char* SERVICE_HOST = "127.0.0.1";
constexpr int AUTH_SERVICE_PORT = 6001;
constexpr int ROOM_SERVICE_PORT = 6002;
constexpr int MESSAGE_SERVICE_PORT = 6003;

SessionManager g_sessions;

struct ClientContext {
    SOCKET socket = INVALID_SOCKET;
    SessionPtr session;   // nullptr, пока не залогинен
};

// Потокобезопасная отправка в сессию.
bool SendToSession(const SessionPtr& session, MessageType type, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(session->sendMutex);
    return SendFrame(session->socket, static_cast<uint16_t>(type), 0, payload) == FrameResult::Ok;
}

// Разослать всем залогиненным клиентам, независимо от комнат.
int BroadcastToAll(MessageType type, const std::vector<uint8_t>& payload, int64_t excludeUserId = 0) {
    int delivered = 0;
    for (const auto& session : g_sessions.GetAllSessions()) {
        if (session->userId == excludeUserId) continue;
        if (SendToSession(session, type, payload)) delivered++;
    }
    return delivered;
}

// Разослать всем онлайн-участникам комнаты. excludeUserId=0 — отправить всем без исключений.
int BroadcastToRoom(int64_t roomId, MessageType type, const std::vector<uint8_t>& payload, int64_t excludeUserId = 0) {
    RoomMembersRequestPayload membersRequest;
    membersRequest.roomId = roomId;

    Frame membersResponse;
    if (!CallService(SERVICE_HOST, ROOM_SERVICE_PORT, MessageType::RoomMembersRequest,
        membersRequest.Serialize(), MessageType::RoomMembersResponse, membersResponse)) {
        return 0;
    }
    auto members = RoomMembersResponsePayload::Deserialize(membersResponse.payload);

    int delivered = 0;
    for (const auto& session : g_sessions.GetSessionsForUsers(members.userIds)) {
        if (session->userId == excludeUserId) continue;
        if (SendToSession(session, type, payload)) delivered++;
    }
    return delivered;
}

void HandleAuth(ClientContext& ctx, const Frame& frame, bool isRegister) {
    auto request = AuthRequestPayload::Deserialize(frame.payload);

    Frame authResponse;
    bool ok = CallService(SERVICE_HOST, AUTH_SERVICE_PORT,
        isRegister ? MessageType::RegisterRequest : MessageType::AuthRequest,
        frame.payload,
        isRegister ? MessageType::RegisterResponse : MessageType::AuthResponse,
        authResponse);

    AuthResponsePayload response;
    if (!ok) {
        response.status = 9;
        std::cout << "[gateway] auth_service unavailable" << std::endl;
    }
    else {
        response = AuthResponsePayload::Deserialize(authResponse.payload);

        if (response.status == 0 && !isRegister) {
            ctx.session = g_sessions.AddSession(response.userId, request.username, ctx.socket);
            response.sessionId = ctx.session->sessionId;
            std::cout << "[gateway] '" << request.username << "' logged in (userId="
                << response.userId << "), online=" << g_sessions.OnlineCount() << std::endl;
        }
    }

    SendFrame(ctx.socket,
        static_cast<uint16_t>(isRegister ? MessageType::RegisterResponse : MessageType::AuthResponse),
        frame.sequence, response.Serialize());
}

// Проксирование команд комнат: gateway подставляет userId из сессии,
// чтобы клиент не мог вступить в комнату от чужого имени.
void HandleRoomMembership(ClientContext& ctx, const Frame& frame, bool isJoin) {
    auto clientRequest = RoomMembershipRequestPayload::Deserialize(frame.payload);

    RoomMembershipRequestPayload serviceRequest;
    serviceRequest.roomId = clientRequest.roomId;
    serviceRequest.userId = ctx.session->userId;   // из сессии, не из запроса

    Frame roomResponse;
    bool ok = CallService(SERVICE_HOST, ROOM_SERVICE_PORT,
        isJoin ? MessageType::RoomJoinRequest : MessageType::RoomLeaveRequest,
        serviceRequest.Serialize(),
        isJoin ? MessageType::RoomJoinResponse : MessageType::RoomLeaveResponse,
        roomResponse);

    StatusResponsePayload response;
    response.status = ok ? StatusResponsePayload::Deserialize(roomResponse.payload).status : 9;

    SendToSession(ctx.session,
        isJoin ? MessageType::JoinRoomResponse : MessageType::RoomLeaveResponse,
        response.Serialize());

    if (response.status == 0) {
        UserPresencePayload presence;
        presence.roomId = serviceRequest.roomId;
        presence.userId = ctx.session->userId;
        presence.username = ctx.session->username;

        // При join уведомляем остальных; при leave — тоже остальных,
        // но сам ушедший уже не в списке участников, так что exclude не нужен.
        BroadcastToRoom(serviceRequest.roomId,
            isJoin ? MessageType::UserJoined : MessageType::UserLeft,
            presence.Serialize(),
            isJoin ? ctx.session->userId : 0);

        std::cout << "[gateway] userId=" << ctx.session->userId
            << (isJoin ? " joined" : " left") << " roomId=" << serviceRequest.roomId << std::endl;
    }
}

void HandleRoomList(ClientContext& ctx, const Frame& frame) {
    Frame roomResponse;
    if (!CallService(SERVICE_HOST, ROOM_SERVICE_PORT, MessageType::RoomListRequest, {},
        MessageType::RoomListResponse, roomResponse)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoomListResponse, roomResponse.payload);
}

void HandleRoomCreate(ClientContext& ctx, const Frame& frame) {
    Frame roomResponse;
    if (!CallService(SERVICE_HOST, ROOM_SERVICE_PORT, MessageType::RoomCreateRequest, frame.payload,
        MessageType::RoomCreateResponse, roomResponse)) {
        return;
    }

    // Ответ создателю
    SendToSession(ctx.session, MessageType::RoomCreateResponse, roomResponse.payload);

    // Если создание удалось — уведомляем остальных
    auto created = RoomCreateResponsePayload::Deserialize(roomResponse.payload);
    if (created.status == 0) {
        auto request = RoomCreateRequestPayload::Deserialize(frame.payload);

        RoomCreatedPayload notification;
        notification.roomId = created.roomId;
        notification.name = request.name;

        // Создателя исключаем: он уже узнал о комнате из RoomCreateResponse
        int notified = BroadcastToAll(MessageType::RoomCreated, notification.Serialize(), ctx.session->userId);
        std::cout << "[gateway] Room '" << request.name << "' (id=" << created.roomId
            << ") created, notified " << notified << " users" << std::endl;
    }
}

void HandleHistory(ClientContext& ctx, const Frame& frame) {
    Frame historyResponse;
    if (!CallService(SERVICE_HOST, MESSAGE_SERVICE_PORT, MessageType::HistoryRequest, frame.payload,
        MessageType::HistoryResponse, historyResponse)) {
        return;
    }
    SendToSession(ctx.session, MessageType::HistoryResponse, historyResponse.payload);
}

void HandleTextMessage(ClientContext& ctx, const Frame& frame) {
    auto clientMessage = ClientTextMessagePayload::Deserialize(frame.payload);

    // 1. Сохранить в message_service
    SendMessageRequestPayload saveRequest;
    saveRequest.roomId = clientMessage.roomId;
    saveRequest.senderId = ctx.session->userId;
    saveRequest.text = clientMessage.text;

    Frame saveResponse;
    if (!CallService(SERVICE_HOST, MESSAGE_SERVICE_PORT, MessageType::SendMessageRequest,
        saveRequest.Serialize(), MessageType::SendMessageResponse, saveResponse)) {
        std::cout << "[gateway] message_service unavailable" << std::endl;
        return;
    }

    auto saved = SendMessageResponsePayload::Deserialize(saveResponse.payload);
    if (saved.status != 0) {
        std::cout << "[gateway] Message rejected, status=" << static_cast<int>(saved.status) << std::endl;
        return;
    }

    // 2. Собрать payload для рассылки
    BroadcastTextMessagePayload broadcast;
    broadcast.messageId = saved.messageId;
    broadcast.roomId = clientMessage.roomId;
    broadcast.senderId = ctx.session->userId;
    broadcast.senderName = ctx.session->username;
    broadcast.timestamp = saved.timestamp;
    broadcast.text = clientMessage.text;

    // 3. Разослать участникам комнаты (включая отправителя)
    int delivered = BroadcastToRoom(clientMessage.roomId, MessageType::TextMessage, broadcast.Serialize());

    std::cout << "[gateway] Message " << saved.messageId << " from '" << ctx.session->username
        << "' delivered to " << delivered << " online users" << std::endl;
}

void ClientThread(SOCKET clientSocket) {
    ClientContext ctx;
    ctx.socket = clientSocket;
    std::cout << "[gateway] Client connected" << std::endl;

    while (true) {
        Frame frame;
        FrameResult result = ReceiveFrame(ctx.socket, frame);

        if (result != FrameResult::Ok) {
            std::cout << "[gateway] Client disconnected" << std::endl;
            break;
        }

        MessageType type = static_cast<MessageType>(frame.messageType);
        bool isAuthenticated = (ctx.session != nullptr);

        if (!isAuthenticated
            && type != MessageType::AuthRequest
            && type != MessageType::RegisterRequest
            && type != MessageType::Ping) {
            std::cout << "[gateway] Rejected 0x" << std::hex << frame.messageType
                << std::dec << " from unauthenticated client" << std::endl;
            continue;
        }

        switch (type) {
        case MessageType::RegisterRequest:   HandleAuth(ctx, frame, true);           break;
        case MessageType::AuthRequest:       HandleAuth(ctx, frame, false);          break;
        case MessageType::RoomCreateRequest: HandleRoomCreate(ctx, frame);           break;
        case MessageType::RoomListRequest:   HandleRoomList(ctx, frame);             break;
        case MessageType::JoinRoom:          HandleRoomMembership(ctx, frame, true); break;
        case MessageType::LeaveRoom:         HandleRoomMembership(ctx, frame, false);break;
        case MessageType::HistoryRequest:    HandleHistory(ctx, frame);              break;
        case MessageType::TextMessage:       HandleTextMessage(ctx, frame);          break;
        case MessageType::Ping:
            SendToSession(ctx.session, MessageType::Pong, frame.payload);
            break;
        default:
            std::cout << "[gateway] Unhandled 0x" << std::hex << frame.messageType
                << std::dec << std::endl;
            break;
        }
    }

    if (ctx.session) {
        g_sessions.RemoveSession(ctx.session->sessionId);
        std::cout << "[gateway] Session closed, online=" << g_sessions.OnlineCount() << std::endl;
    }
    closesocket(ctx.socket);
}

int main() {
    WinsockGuard winsock;
    if (!winsock.IsInitialized()) {
        std::cerr << "[gateway] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(GATEWAY_PORT);

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[gateway] Bind failed" << std::endl;
        return 1;
    }
    listen(listenSocket, SOMAXCONN);

    std::cout << "[gateway] Listening on port " << GATEWAY_PORT << std::endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) continue;
        std::thread(ClientThread, clientSocket).detach();
    }

    return 0;
}