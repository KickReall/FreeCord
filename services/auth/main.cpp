#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"

constexpr int AUTH_SERVICE_PORT = 6001;

void HandleClient(SOCKET clientSocket) {
    Frame frame;
    FrameResult result = ReceiveFrame(clientSocket, frame);

    if (result != FrameResult::Ok) {
        std::cout << "[auth] Failed to receive frame from client" << std::endl;
        closesocket(clientSocket);
        return;
    }

    if (frame.messageType != static_cast<uint16_t>(MessageType::AuthRequest)) {
        std::cout << "[auth] Unexpected messageType: " << frame.messageType << std::endl;
        closesocket(clientSocket);
        return;
    }

    AuthRequestPayload request = AuthRequestPayload::Deserialize(frame.payload);
    std::cout << "[auth] AuthRequest from username=" << request.username << std::endl;

    // Заглушка: пока принимаем любой логин/пароль как валидный.
    // Реальная проверка через SQLite появится позже.
    AuthResponsePayload response;
    response.status = 0; // success
    response.sessionId = 123456789ULL;
    response.userId = 1;

    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::AuthResponse), frame.sequence, response.Serialize());

    closesocket(clientSocket);
}

int main() {
    WinsockGuard winsock;
    if (!winsock.IsInitialized()) {
        std::cerr << "[auth] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "[auth] Failed to create socket" << std::endl;
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(AUTH_SERVICE_PORT);

    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[auth] Bind failed" << std::endl;
        closesocket(listenSocket);
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[auth] Listen failed" << std::endl;
        closesocket(listenSocket);
        return 1;
    }

    std::cout << "[auth] Listening on port " << AUTH_SERVICE_PORT << std::endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "[auth] Accept failed" << std::endl;
            continue;
        }
        std::cout << "[auth] Client connected" << std::endl;
        HandleClient(clientSocket); // однопоточно, по одному клиенту за раз — этого достаточно для проверки
    }

    closesocket(listenSocket);
    return 0;
}