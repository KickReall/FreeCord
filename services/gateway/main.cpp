#include <iostream>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "ServiceClient.h"
#include "SessionManager.h"

constexpr int GATEWAY_PORT = 6000;
constexpr const char* SERVICE_HOST = "127.0.0.1";
constexpr int AUTH_SERVICE_PORT = 6001;

SessionManager g_sessions;

// Состояние одного подключённого клиента — живёт в его собственном потоке.
struct ClientContext {
    SOCKET socket = INVALID_SOCKET;
    uint64_t sessionId = 0;
    int64_t userId = 0;
    bool authenticated = false;
};

void HandleAuth(ClientContext& ctx, const Frame& frame, bool isRegister) {
    auto request = AuthRequestPayload::Deserialize(frame.payload);

    // Проксируем запрос в auth_service как есть — gateway не знает про пароли и хеши.
    Frame authResponse;
    bool ok = CallService(SERVICE_HOST, AUTH_SERVICE_PORT,
        isRegister ? MessageType::RegisterRequest : MessageType::AuthRequest,
        frame.payload,
        isRegister ? MessageType::RegisterResponse : MessageType::AuthResponse,
        authResponse);

    AuthResponsePayload response;
    if (!ok) {
        response.status = 9; // auth service unavailable
        std::cout << "[gateway] auth_service unavailable" << std::endl;
    }
    else {
        response = AuthResponsePayload::Deserialize(authResponse.payload);

        // Сессию заводим только на успешный ЛОГИН, не на регистрацию.
        if (response.status == 0 && !isRegister) {
            ctx.userId = response.userId;
            ctx.sessionId = g_sessions.AddSession(ctx.userId, request.username, ctx.socket);
            ctx.authenticated = true;
            response.sessionId = ctx.sessionId; // подменяем заглушку auth на реальный id сессии
            std::cout << "[gateway] User '" << request.username << "' logged in"
                << " (userId=" << ctx.userId << ", sessionId=" << ctx.sessionId << ")"
                << ", online=" << g_sessions.OnlineCount() << std::endl;
        }
    }

    SendFrame(ctx.socket,
        static_cast<uint16_t>(isRegister ? MessageType::RegisterResponse : MessageType::AuthResponse),
        frame.sequence, response.Serialize());
}

void HandlePing(ClientContext& ctx, const Frame& frame) {
    // Эхо: возвращаем payload как есть, клиент по нему померяет RTT.
    SendFrame(ctx.socket, static_cast<uint16_t>(MessageType::Pong), frame.sequence, frame.payload);
}

// Один поток на клиента. Соединение живёт, пока клиент не отключится.
void ClientThread(SOCKET clientSocket) {
    ClientContext ctx;
    ctx.socket = clientSocket;

    std::cout << "[gateway] Client connected" << std::endl;

    while (true) {
        Frame frame;
        FrameResult result = ReceiveFrame(ctx.socket, frame);

        if (result == FrameResult::ConnectionClosed) {
            std::cout << "[gateway] Client disconnected (userId=" << ctx.userId << ")" << std::endl;
            break;
        }
        if (result != FrameResult::Ok) {
            std::cout << "[gateway] Receive error (userId=" << ctx.userId << ")" << std::endl;
            break;
        }

        MessageType type = static_cast<MessageType>(frame.messageType);

        // До логина разрешены только регистрация и вход.
        if (!ctx.authenticated
            && type != MessageType::AuthRequest
            && type != MessageType::RegisterRequest
            && type != MessageType::Ping) {
            std::cout << "[gateway] Rejected message 0x" << std::hex << frame.messageType
                << std::dec << " from unauthenticated client" << std::endl;
            continue;
        }

        switch (type) {
        case MessageType::RegisterRequest: HandleAuth(ctx, frame, true);  break;
        case MessageType::AuthRequest:     HandleAuth(ctx, frame, false); break;
        case MessageType::Ping:            HandlePing(ctx, frame);        break;
        default:
            std::cout << "[gateway] Unhandled messageType: 0x" << std::hex
                << frame.messageType << std::dec << std::endl;
            break;
        }
    }

    if (ctx.sessionId != 0) {
        g_sessions.RemoveSession(ctx.sessionId);
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

        // detach: поток живёт сам по себе, main продолжает принимать новых клиентов.
        std::thread(ClientThread, clientSocket).detach();
    }

    closesocket(listenSocket);
    return 0;
}