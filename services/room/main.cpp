#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "RoomMessages.h"
#include "RoomRepository.h"


constexpr int ROOM_SERVICE_PORT = 6002;
constexpr const char* DB_PATH = "freecord_rooms.db";

void HandleCreate(SOCKET sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomCreateRequestPayload::Deserialize(frame.payload);
    std::cout << "[room] CreateRoom name=" << request.name << std::endl;

    RoomCreateResponsePayload response;
    int64_t roomId = repo.CreateRoom(request.name);
    if (roomId == -1) {
        response.status = 1;
        std::cout << "[room] Create failed: name taken" << std::endl;
    }
    else {
        response.status = 0;
        response.roomId = roomId;
        std::cout << "[room] Room created, id=" << roomId << std::endl;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::RoomCreateResponse), frame.sequence, response.Serialize());
}

void HandleJoin(SOCKET sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    std::cout << "[room] Join roomId=" << request.roomId << " userId=" << request.userId << std::endl;

    StatusResponsePayload response;
    if (!repo.RoomExists(request.roomId)) {
        response.status = 1; // room not found
    }
    else if (!repo.AddMember(request.roomId, request.userId)) {
        response.status = 2; // already a member
    }
    else {
        response.status = 0;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::RoomJoinResponse), frame.sequence, response.Serialize());
}

void HandleLeave(SOCKET sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    std::cout << "[room] Leave roomId=" << request.roomId << " userId=" << request.userId << std::endl;

    StatusResponsePayload response;
    if (!repo.RoomExists(request.roomId)) {
        response.status = 1;
    }
    else if (!repo.RemoveMember(request.roomId, request.userId)) {
        response.status = 2; // not a member
    }
    else {
        response.status = 0;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::RoomLeaveResponse), frame.sequence, response.Serialize());
}

void HandleList(SOCKET sock, RoomRepository& repo, const Frame& frame) {
    std::cout << "[room] ListRooms" << std::endl;
    RoomListResponsePayload response;
    for (const auto& record : repo.ListRooms()) {
        response.rooms.push_back(RoomInfo{ record.id, record.name });
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::RoomListResponse), frame.sequence, response.Serialize());
}

void HandleMembers(SOCKET sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembersRequestPayload::Deserialize(frame.payload);
    std::cout << "[room] ListMembers roomId=" << request.roomId << std::endl;

    RoomMembersResponsePayload response;
    response.userIds = repo.ListMembers(request.roomId);
    SendFrame(sock, static_cast<uint16_t>(MessageType::RoomMembersResponse), frame.sequence, response.Serialize());
}

void HandleClient(SOCKET sock, RoomRepository& repo) {
    Frame frame;
    if (ReceiveFrame(sock, frame) != FrameResult::Ok) {
        std::cout << "[room] Failed to receive frame" << std::endl;
        closesocket(sock);
        return;
    }

    switch (static_cast<MessageType>(frame.messageType)) {
    case MessageType::RoomCreateRequest:  HandleCreate(sock, repo, frame);  break;
    case MessageType::RoomJoinRequest:    HandleJoin(sock, repo, frame);    break;
    case MessageType::RoomLeaveRequest:   HandleLeave(sock, repo, frame);   break;
    case MessageType::RoomListRequest:    HandleList(sock, repo, frame);    break;
    case MessageType::RoomMembersRequest: HandleMembers(sock, repo, frame); break;
    default:
        std::cout << "[room] Unexpected messageType: " << frame.messageType << std::endl;
        break;
    }

    closesocket(sock);
}

int main() {
    WinsockGuard winsock;
    if (!winsock.IsInitialized()) {
        std::cerr << "[room] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    RoomRepository repo(DB_PATH);
    std::cout << "[room] Database ready at " << DB_PATH << std::endl;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(ROOM_SERVICE_PORT);

    bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    std::cout << "[room] Listening on port " << ROOM_SERVICE_PORT << std::endl;

    while (true) {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) continue;
        std::thread(HandleClient, clientSocket, std::ref(repo)).detach();
    }

    closesocket(listenSocket);
    return 0;
}