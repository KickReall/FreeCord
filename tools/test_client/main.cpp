#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"

constexpr const char* AUTH_SERVICE_HOST = "127.0.0.1";
constexpr int AUTH_SERVICE_PORT = 6001;

int main() {
    WinsockGuard winsock;
    if (!winsock.IsInitialized()) {
        std::cerr << "[client] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "[client] Failed to create socket" << std::endl;
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(AUTH_SERVICE_PORT);
    inet_pton(AF_INET, AUTH_SERVICE_HOST, &serverAddr.sin_addr);

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[client] Failed to connect to auth_service. Is it running?" << std::endl;
        closesocket(clientSocket);
        return 1;
    }

    std::cout << "[client] Connected to auth_service" << std::endl;

    AuthRequestPayload request;
    request.username = "testuser";
    request.password = "testpassword";

    FrameResult sendResult = SendFrame(clientSocket, static_cast<uint16_t>(MessageType::AuthRequest), 1, request.Serialize());
    if (sendResult != FrameResult::Ok) {
        std::cerr << "[client] Failed to send AuthRequest" << std::endl;
        closesocket(clientSocket);
        return 1;
    }

    std::cout << "[client] AuthRequest sent, waiting for response..." << std::endl;

    Frame responseFrame;
    FrameResult receiveResult = ReceiveFrame(clientSocket, responseFrame);
    if (receiveResult != FrameResult::Ok) {
        std::cerr << "[client] Failed to receive response" << std::endl;
        closesocket(clientSocket);
        return 1;
    }

    if (responseFrame.messageType != static_cast<uint16_t>(MessageType::AuthResponse)) {
        std::cerr << "[client] Unexpected messageType in response: " << responseFrame.messageType << std::endl;
        closesocket(clientSocket);
        return 1;
    }

    AuthResponsePayload response = AuthResponsePayload::Deserialize(responseFrame.payload);
    std::cout << "[client] AuthResponse received:" << std::endl;
    std::cout << "  status = " << static_cast<int>(response.status) << std::endl;
    std::cout << "  sessionId = " << response.sessionId << std::endl;
    std::cout << "  userId = " << response.userId << std::endl;

    closesocket(clientSocket);
    return 0;
}