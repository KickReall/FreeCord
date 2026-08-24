#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "UserRepository.h"
#include "PasswordHasher.h"

constexpr int AUTH_SERVICE_PORT = 6001;
constexpr const char* DB_PATH = "freecord_auth.db";

void HandleRegister(SOCKET clientSocket, UserRepository& repo, const Frame& frame) {
    AuthRequestPayload request = AuthRequestPayload::Deserialize(frame.payload);
    std::cout << "[auth] RegisterRequest username=" << request.username << std::endl;

    std::string salt = PasswordHasher::GenerateSalt();
    std::string hash = PasswordHasher::HashPassword(request.password, salt);

    int64_t userId = repo.CreateUser(request.username, hash, salt);

    AuthResponsePayload response;
    if (userId == -1) {
        response.status = 1; // username taken
        std::cout << "[auth] Register failed: username taken" << std::endl;
    }
    else {
        response.status = 0;
        response.userId = static_cast<uint32_t>(userId);
        response.sessionId = 0; // сессия не нужна сразу после регистрации, только после логина
        std::cout << "[auth] User registered, id=" << userId << std::endl;
    }

    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::RegisterResponse), frame.sequence, response.Serialize());
}

void HandleLogin(SOCKET clientSocket, UserRepository& repo, const Frame& frame) {
    AuthRequestPayload request = AuthRequestPayload::Deserialize(frame.payload);
    std::cout << "[auth] AuthRequest username=" << request.username << std::endl;

    AuthResponsePayload response;
    auto user = repo.FindByUsername(request.username);

    if (!user || !PasswordHasher::VerifyPassword(request.password, user->passwordSalt, user->passwordHash)) {
        response.status = 1; // invalid credentials
        std::cout << "[auth] Login failed: invalid credentials" << std::endl;
    }
    else {
        response.status = 0;
        response.userId = static_cast<uint32_t>(user->id);
        response.sessionId = 123456789ULL; // заглушка, реальную генерацию sessionId добавим позже
        std::cout << "[auth] Login success, userId=" << user->id << std::endl;
    }

    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::AuthResponse), frame.sequence, response.Serialize());
}

void HandleClient(SOCKET clientSocket, UserRepository& repo) {
    Frame frame;
    FrameResult result = ReceiveFrame(clientSocket, frame);

    if (result != FrameResult::Ok) {
        std::cout << "[auth] Failed to receive frame" << std::endl;
        closesocket(clientSocket);
        return;
    }

    if (frame.messageType == static_cast<uint16_t>(MessageType::RegisterRequest)) {
        HandleRegister(clientSocket, repo, frame);
    }
    else if (frame.messageType == static_cast<uint16_t>(MessageType::AuthRequest)) {
        HandleLogin(clientSocket, repo, frame);
    }
    else {
        std::cout << "[auth] Unexpected messageType: " << frame.messageType << std::endl;
    }

    closesocket(clientSocket);
}

int main() {
    WinsockGuard winsock;
    if (!winsock.IsInitialized()) {
        std::cerr << "[auth] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    UserRepository repo(DB_PATH);
    std::cout << "[auth] Database ready at " << DB_PATH << std::endl;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(AUTH_SERVICE_PORT);

    bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    std::cout << "[auth] Listening on port " << AUTH_SERVICE_PORT << std::endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) continue;
        std::cout << "[auth] Client connected" << std::endl;
        HandleClient(clientSocket, repo);
    }

    closesocket(listenSocket);
    return 0;
}