#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"

constexpr const char* AUTH_SERVICE_HOST = "127.0.0.1";
constexpr int AUTH_SERVICE_PORT = 6001;

SOCKET ConnectToAuthService() {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(AUTH_SERVICE_PORT);
    inet_pton(AF_INET, AUTH_SERVICE_HOST, &serverAddr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

void DoRequest(MessageType requestType, MessageType expectedResponseType,
    const std::string& username, const std::string& password) {
    SOCKET sock = ConnectToAuthService();
    if (sock == INVALID_SOCKET) {
        std::cerr << "[client] Failed to connect. Is auth_service running?" << std::endl;
        return;
    }

    AuthRequestPayload request;
    request.username = username;
    request.password = password;

    if (SendFrame(sock, static_cast<uint16_t>(requestType), 1, request.Serialize()) != FrameResult::Ok) {
        std::cerr << "[client] Failed to send request" << std::endl;
        closesocket(sock);
        return;
    }

    Frame responseFrame;
    if (ReceiveFrame(sock, responseFrame) != FrameResult::Ok) {
        std::cerr << "[client] Failed to receive response" << std::endl;
        closesocket(sock);
        return;
    }

    if (responseFrame.messageType != static_cast<uint16_t>(expectedResponseType)) {
        std::cerr << "[client] Unexpected messageType: " << responseFrame.messageType << std::endl;
        closesocket(sock);
        return;
    }

    AuthResponsePayload response = AuthResponsePayload::Deserialize(responseFrame.payload);
    std::cout << "  status    = " << static_cast<int>(response.status)
        << (response.status == 0 ? " (success)" : " (failed)") << std::endl;
    std::cout << "  userId    = " << response.userId << std::endl;
    std::cout << "  sessionId = " << response.sessionId << std::endl;

    closesocket(sock);
}

int main() {
    WinsockGuard winsock;
    if (!winsock.IsInitialized()) {
        std::cerr << "[client] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    while (true) {
        std::cout << "\n=== FreeCord test client ===" << std::endl;
        std::cout << "1 - Register" << std::endl;
        std::cout << "2 - Login" << std::endl;
        std::cout << "0 - Exit" << std::endl;
        std::cout << "> ";

        int choice = 0;
        if (!(std::cin >> choice)) break;
        if (choice == 0) break;

        std::string username, password;
        std::cout << "Username: ";
        std::cin >> username;
        std::cout << "Password: ";
        std::cin >> password;

        if (choice == 1) {
            std::cout << "[client] Sending RegisterRequest..." << std::endl;
            DoRequest(MessageType::RegisterRequest, MessageType::RegisterResponse, username, password);
        }
        else if (choice == 2) {
            std::cout << "[client] Sending AuthRequest..." << std::endl;
            DoRequest(MessageType::AuthRequest, MessageType::AuthResponse, username, password);
        }
    }

    std::cout << "[client] Bye" << std::endl;
    return 0;
}