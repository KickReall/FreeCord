#include <iostream>
#include <thread>
#include <chrono>
#include <memory>

#include "PlatformSocket.h"
#include "TcpFramer.h"
#include "TlsContext.h"
#include "TlsTransport.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "RoomMessages.h"
#include "MessageMessages.h"
#include "ChatMessages.h"
#include "RoleMessages.h"
#include "Permissions.h"
#include "ServiceClient.h"
#include "SessionManager.h"
#include "Config.h"

// Внутренний инвариант, завязанный на seed-данные в db/room/migrations/001_initial.sql —
// не выносим в config.json, иначе конфиг и БД могут разойтись.
constexpr int64_t SYSTEM_ROOM_ID = 1;

SessionManager g_sessions;
AppConfig g_config;
std::unique_ptr<TlsContext> g_tls;

struct ClientContext {
    socket_t socket = kInvalidSocket;         // нужен для SetRecvTimeout/CloseSocket — TLS работает поверх него
    std::shared_ptr<ITransport> transport;    // TLS-соединение с клиентом; до успешного handshake равен nullptr
    SessionPtr session;   // nullptr, пока не залогинен
};

// Потокобезопасная отправка в сессию.
bool SendToSession(const SessionPtr& session, MessageType type, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(session->sendMutex);
    return SendFrame(*session->transport, static_cast<uint16_t>(type), 0, payload) == FrameResult::Ok;
}

bool HasPermission(const SessionPtr& session, Permission permission) {
    return (session->permissions.load() & static_cast<uint32_t>(permission)) != 0;
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

// Разослать всем, кто сейчас открыл эту комнату.
int BroadcastToRoom(int64_t roomId, MessageType type, const std::vector<uint8_t>& payload, int64_t excludeUserId = 0) {
    int delivered = 0;
    for (const auto& session : g_sessions.GetSessionsInRoom(roomId)) {
        if (session->userId == excludeUserId) continue;
        if (SendToSession(session, type, payload)) delivered++;
    }
    return delivered;
}

void HandleAuth(ClientContext& ctx, const Frame& frame, bool isRegister) {
    auto request = AuthRequestPayload::Deserialize(frame.payload);

    Frame authResponse;
    bool ok = CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port,
        isRegister ? MessageType::RegisterRequest : MessageType::AuthRequest,
        frame.payload,
        isRegister ? MessageType::RegisterResponse : MessageType::AuthResponse,
        authResponse, g_config.gateway.serviceCallTimeoutMs);

    AuthResponsePayload response;
    if (!ok) {
        response.status = 9;
        std::cout << "[gateway] auth_service unavailable" << std::endl;
    }
    else {
        response = AuthResponsePayload::Deserialize(authResponse.payload);

        if (response.status == 0 && !isRegister) {
            ctx.session = g_sessions.AddSession(response.userId, request.username, ctx.transport);
            response.sessionId = ctx.session->sessionId;
            std::cout << "[gateway] '" << request.username << "' logged in (userId="
                << response.userId << "), online=" << g_sessions.OnlineCount() << std::endl;

            // Права считаются один раз при логине и кэшируются в сессии — изменение
            // ролей применится только после повторного логина (см. Session::permissions).
            Frame permResponse;
            GetUserPermissionsRequestPayload permRequest;
            permRequest.userId = response.userId;
            if (CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port, MessageType::GetUserPermissionsRequest,
                permRequest.Serialize(), MessageType::GetUserPermissionsResponse, permResponse,
                g_config.gateway.serviceCallTimeoutMs)) {
                auto perms = MyPermissionsPayload::Deserialize(permResponse.payload);
                ctx.session->permissions.store(perms.permissions);
                SendToSession(ctx.session, MessageType::MyPermissions, perms.Serialize());
            }
        }

        // Новый пользователь — пишем в системную комнату и уведомляем всех
        if (response.status == 0 && isRegister) {
            std::string text = "Новый пользователь: " + request.username;

            SendMessageRequestPayload sysMessage;
            sysMessage.roomId = SYSTEM_ROOM_ID;
            sysMessage.senderId = 0;
            sysMessage.senderName = "System";
            sysMessage.text = text;

            Frame saveResponse;
            if (CallService(g_config.gateway.serviceHost.c_str(), g_config.message.port, MessageType::SendMessageRequest,
                sysMessage.Serialize(), MessageType::SendMessageResponse, saveResponse,
                g_config.gateway.serviceCallTimeoutMs)) {

                auto saved = SendMessageResponsePayload::Deserialize(saveResponse.payload);
                if (saved.status == 0) {
                    BroadcastTextMessagePayload broadcast;
                    broadcast.messageId = saved.messageId;
                    broadcast.roomId = SYSTEM_ROOM_ID;
                    broadcast.senderId = 0;
                    broadcast.senderName = "System";
                    broadcast.timestamp = saved.timestamp;
                    broadcast.text = text;

                    BroadcastToRoom(SYSTEM_ROOM_ID, MessageType::TextMessage, broadcast.Serialize());
                }
            }

            UserRegisteredPayload notification;
            notification.userId = response.userId;
            notification.username = request.username;
            BroadcastToAll(MessageType::UserRegistered, notification.Serialize());

            std::cout << "[gateway] New user registered: " << request.username << std::endl;
        }
    }

    SendFrame(*ctx.transport,
        static_cast<uint16_t>(isRegister ? MessageType::RegisterResponse : MessageType::AuthResponse),
        frame.sequence, response.Serialize());
}

// Выбор комнаты: просто переключение в сессии, без записей в БД.
void HandleSelectRoom(ClientContext& ctx, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);

    int64_t previous = ctx.session->currentRoomId.exchange(request.roomId);

    StatusResponsePayload response;
    response.status = 0;
    SendToSession(ctx.session, MessageType::JoinRoomResponse, response.Serialize());

    std::cout << "[gateway] userId=" << ctx.session->userId
        << " switched room " << previous << " -> " << request.roomId << std::endl;
}

void HandleLeaveRoom(ClientContext& ctx, const Frame& frame) {
    ctx.session->currentRoomId.store(0);

    StatusResponsePayload response;
    response.status = 0;
    SendToSession(ctx.session, MessageType::RoomLeaveResponse, response.Serialize());
}

void HandleRoomList(ClientContext& ctx, const Frame& frame) {
    Frame roomResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.room.port, MessageType::RoomListRequest, {},
        MessageType::RoomListResponse, roomResponse, g_config.gateway.serviceCallTimeoutMs)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoomListResponse, roomResponse.payload);
}

void HandleRoomCreate(ClientContext& ctx, const Frame& frame) {
    Frame roomResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.room.port, MessageType::RoomCreateRequest, frame.payload,
        MessageType::RoomCreateResponse, roomResponse, g_config.gateway.serviceCallTimeoutMs)) {
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
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.message.port, MessageType::HistoryRequest, frame.payload,
        MessageType::HistoryResponse, historyResponse, g_config.gateway.serviceCallTimeoutMs)) {
        return;
    }
    SendToSession(ctx.session, MessageType::HistoryResponse, historyResponse.payload);
}

void HandleTextMessage(ClientContext& ctx, const Frame& frame) {
    auto clientMessage = ClientTextMessagePayload::Deserialize(frame.payload);

    // Писать можно только в комнату, открытую сейчас
    if (ctx.session->currentRoomId.load() != clientMessage.roomId) {
        std::cout << "[gateway] userId=" << ctx.session->userId
            << " tried to post to room " << clientMessage.roomId
            << " while in " << ctx.session->currentRoomId.load() << std::endl;
        return;
    }

    // 1. Сохранить в message_service
    SendMessageRequestPayload saveRequest;
    saveRequest.roomId = clientMessage.roomId;
    saveRequest.senderId = ctx.session->userId;
    saveRequest.senderName = ctx.session->username;
    saveRequest.text = clientMessage.text;

    Frame saveResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.message.port, MessageType::SendMessageRequest,
        saveRequest.Serialize(), MessageType::SendMessageResponse, saveResponse,
        g_config.gateway.serviceCallTimeoutMs)) {
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

// Список ролей виден любому залогиненному пользователю — сама по себе не секрет.
void HandleRoleList(ClientContext& ctx, const Frame& frame) {
    Frame roleResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port, MessageType::RoleListRequest, {},
        MessageType::RoleListResponse, roleResponse, g_config.gateway.serviceCallTimeoutMs)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoleListResponse, roleResponse.payload);
}

void HandleRoleCreate(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageRoles)) {
        RoleCreateResponsePayload forbidden;
        forbidden.status = 254;
        SendToSession(ctx.session, MessageType::RoleCreateResponse, forbidden.Serialize());
        return;
    }
    Frame roleResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port, MessageType::RoleCreateRequest, frame.payload,
        MessageType::RoleCreateResponse, roleResponse, g_config.gateway.serviceCallTimeoutMs)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoleCreateResponse, roleResponse.payload);
}

void HandleRoleUpdate(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageRoles)) {
        StatusResponsePayload forbidden;
        forbidden.status = 254;
        SendToSession(ctx.session, MessageType::RoleUpdateResponse, forbidden.Serialize());
        return;
    }
    Frame roleResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port, MessageType::RoleUpdateRequest, frame.payload,
        MessageType::RoleUpdateResponse, roleResponse, g_config.gateway.serviceCallTimeoutMs)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoleUpdateResponse, roleResponse.payload);
}

void HandleRoleDelete(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageRoles)) {
        StatusResponsePayload forbidden;
        forbidden.status = 254;
        SendToSession(ctx.session, MessageType::RoleDeleteResponse, forbidden.Serialize());
        return;
    }
    Frame roleResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port, MessageType::RoleDeleteRequest, frame.payload,
        MessageType::RoleDeleteResponse, roleResponse, g_config.gateway.serviceCallTimeoutMs)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoleDeleteResponse, roleResponse.payload);
}

void HandleRoleAssign(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageRoles)) {
        StatusResponsePayload forbidden;
        forbidden.status = 254;
        SendToSession(ctx.session, MessageType::RoleAssignResponse, forbidden.Serialize());
        return;
    }
    Frame roleResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port, MessageType::RoleAssignRequest, frame.payload,
        MessageType::RoleAssignResponse, roleResponse, g_config.gateway.serviceCallTimeoutMs)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoleAssignResponse, roleResponse.payload);
}

void HandleRoleRemove(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageRoles)) {
        StatusResponsePayload forbidden;
        forbidden.status = 254;
        SendToSession(ctx.session, MessageType::RoleRemoveResponse, forbidden.Serialize());
        return;
    }
    Frame roleResponse;
    if (!CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port, MessageType::RoleRemoveRequest, frame.payload,
        MessageType::RoleRemoveResponse, roleResponse, g_config.gateway.serviceCallTimeoutMs)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoleRemoveResponse, roleResponse.payload);
}

void ClientThread(socket_t clientSocket) {
    ClientContext ctx;
    ctx.socket = clientSocket;

    // Ставим таймаут на чтение до хендшейка — иначе зависший на TLS клиент
    // навсегда займёт поток, так же как раньше это грозило голому recv().
    SetRecvTimeout(clientSocket, g_config.gateway.recvTimeoutMs);

    ctx.transport = TlsTransport::AcceptServer(g_tls->Get(), clientSocket);
    if (!ctx.transport) {
        std::cout << "[gateway] TLS handshake failed, dropping connection" << std::endl;
        CloseSocket(ctx.socket);
        return;
    }
    std::cout << "[gateway] Client connected (TLS)" << std::endl;

    auto lastActivity = std::chrono::steady_clock::now();

    while (true) {
        Frame frame;
        FrameResult result = ReceiveFrame(*ctx.transport, frame);

        if (result == FrameResult::Timeout) {
            auto idleSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - lastActivity).count();

            if (idleSeconds >= g_config.gateway.clientIdleTimeoutSec) {
                std::cout << "[gateway] Client timed out (userId="
                    << (ctx.session ? ctx.session->userId : 0)
                    << ", idle " << idleSeconds << "s)" << std::endl;
                break;
            }
            continue;   // просто тишина, ждём дальше
        }

        if (result != FrameResult::Ok) {
            std::cout << "[gateway] Client disconnected" << std::endl;
            break;
        }

        lastActivity = std::chrono::steady_clock::now();

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
        case MessageType::RegisterRequest:   HandleAuth(ctx, frame, true);  break;
        case MessageType::AuthRequest:       HandleAuth(ctx, frame, false); break;
        case MessageType::RoomCreateRequest: HandleRoomCreate(ctx, frame);  break;
        case MessageType::RoomListRequest:   HandleRoomList(ctx, frame);    break;
        case MessageType::JoinRoom:          HandleSelectRoom(ctx, frame);  break;
        case MessageType::LeaveRoom:         HandleLeaveRoom(ctx, frame);   break;
        case MessageType::HistoryRequest:    HandleHistory(ctx, frame);     break;
        case MessageType::TextMessage:       HandleTextMessage(ctx, frame); break;
        case MessageType::RoleListRequest:   HandleRoleList(ctx, frame);    break;
        case MessageType::RoleCreateRequest: HandleRoleCreate(ctx, frame);  break;
        case MessageType::RoleUpdateRequest: HandleRoleUpdate(ctx, frame);  break;
        case MessageType::RoleDeleteRequest: HandleRoleDelete(ctx, frame);  break;
        case MessageType::RoleAssignRequest: HandleRoleAssign(ctx, frame);  break;
        case MessageType::RoleRemoveRequest: HandleRoleRemove(ctx, frame);  break;
        case MessageType::Ping:
            // Отвечаем только залогиненным — до логина сессии ещё нет
            if (ctx.session) SendToSession(ctx.session, MessageType::Pong, frame.payload);
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
    // Отпускаем TLS-объект (шлёт close_notify) до закрытия самого сокета, а не после.
    ctx.session.reset();
    ctx.transport.reset();
    CloseSocket(ctx.socket);
}

int main() {
    try {
        g_config = LoadConfig();
    }
    catch (const std::exception& ex) {
        std::cerr << "[gateway] Config error: " << ex.what() << std::endl;
        return 1;
    }

    SocketLibraryGuard socketLibrary;
    if (!socketLibrary.IsInitialized()) {
        std::cerr << "[gateway] Failed to initialize socket library" << std::endl;
        return 1;
    }

    try {
        g_tls = std::make_unique<TlsContext>(g_config.gateway.tls.certPath, g_config.gateway.tls.keyPath);
    }
    catch (const std::exception& ex) {
        std::cerr << "[gateway] TLS error: " << ex.what() << std::endl;
        return 1;
    }
    std::cout << "[gateway] TLS certificate fingerprint (SHA-256): " << g_tls->FingerprintHex() << std::endl;

    socket_t listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(g_config.gateway.port);

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1) {
        std::cerr << "[gateway] Bind failed" << std::endl;
        return 1;
    }
    listen(listenSocket, SOMAXCONN);

    std::cout << "[gateway] Listening on port " << g_config.gateway.port << std::endl;

    while (true) {
        socket_t clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == kInvalidSocket) continue;
        std::thread(ClientThread, clientSocket).detach();
    }

    return 0;
}