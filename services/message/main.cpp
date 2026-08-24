#include <iostream>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "MessageMessages.h"
#include "MessageRepository.h"

constexpr int MESSAGE_SERVICE_PORT = 6003;
constexpr const char* DB_PATH = "freecord_messages.db";
constexpr size_t MAX_TEXT_LENGTH = 4000;

int64_t CurrentUnixTime() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void HandleSend(SOCKET sock, MessageRepository& repo, const Frame& frame) {
    auto request = SendMessageRequestPayload::Deserialize(frame.payload);

    SendMessageResponsePayload response;

    if (request.text.empty()) {
        response.status = 1;
        std::cout << "[message] Rejected: empty text" << std::endl;
    }
    else if (request.text.size() > MAX_TEXT_LENGTH) {
        response.status = 2;
        std::cout << "[message] Rejected: text too long (" << request.text.size() << ")" << std::endl;
    }
    else {
        int64_t timestamp = CurrentUnixTime();
        int64_t messageId = repo.SaveMessage(request.roomId, request.senderId, request.text, timestamp);
        if (messageId == -1) {
            response.status = 3; // storage error
            std::cout << "[message] Storage error" << std::endl;
        }
        else {
            response.status = 0;
            response.messageId = messageId;
            response.timestamp = timestamp;
            std::cout << "[message] Saved id=" << messageId
                << " roomId=" << request.roomId
                << " senderId=" << request.senderId << std::endl;
        }
    }

    SendFrame(sock, static_cast<uint16_t>(MessageType::SendMessageResponse), frame.sequence, response.Serialize());
}

void HandleHistory(SOCKET sock, MessageRepository& repo, const Frame& frame) {
    auto request = HistoryRequestPayload::Deserialize(frame.payload);
    std::cout << "[message] History roomId=" << request.roomId << " limit=" << request.limit << std::endl;

    HistoryResponsePayload response;
    response.messages = repo.GetHistory(request.roomId, request.limit);

    SendFrame(sock, static_cast<uint16_t>(MessageType::HistoryResponse), frame.sequence, response.Serialize());
}

void HandleClient(SOCKET sock, MessageRepository& repo) {
    Frame frame;
    if (ReceiveFrame(sock, frame) != FrameResult::Ok) {
        std::cout << "[message] Failed to receive frame" << std::endl;
        closesocket(sock);
        return;
    }

    switch (static_cast<MessageType>(frame.messageType)) {
    case MessageType::SendMessageRequest: HandleSend(sock, repo, frame);    break;
    case MessageType::HistoryRequest:     HandleHistory(sock, repo, frame); break;
    default:
        std::cout << "[message] Unexpected messageType: " << frame.messageType << std::endl;
        break;
    }

    closesocket(sock);
}

int main() {
    WinsockGuard winsock;
    if (!winsock.IsInitialized()) {
        std::cerr << "[message] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    MessageRepository repo(DB_PATH);
    std::cout << "[message] Database ready at " << DB_PATH << std::endl;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(MESSAGE_SERVICE_PORT);

    bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    std::cout << "[message] Listening on port " << MESSAGE_SERVICE_PORT << std::endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) continue;
        std::thread(HandleClient, clientSocket, std::ref(repo)).detach();
    }

    closesocket(listenSocket);
    return 0;
}