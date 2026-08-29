#include <iostream>
#include <thread>
#include <memory>

#include "PlatformSocket.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "RoomMessages.h"
#include "RoomRepository.h"
#include "Config.h"

void HandleCreate(socket_t sock, RoomRepository& repo, const Frame& frame) {
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

void HandleJoin(socket_t sock, RoomRepository& repo, const Frame& frame) {
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

void HandleLeave(socket_t sock, RoomRepository& repo, const Frame& frame) {
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

void HandleList(socket_t sock, RoomRepository& repo, const Frame& frame) {
    std::cout << "[room] ListRooms" << std::endl;
    RoomListResponsePayload response;
    for (const auto& record : repo.ListRooms()) {
        response.rooms.push_back(RoomInfo{ record.id, record.name });
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::RoomListResponse), frame.sequence, response.Serialize());
}

void HandleMembers(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembersRequestPayload::Deserialize(frame.payload);
    std::cout << "[room] ListMembers roomId=" << request.roomId << std::endl;

    RoomMembersResponsePayload response;
    response.userIds = repo.ListMembers(request.roomId);
    SendFrame(sock, static_cast<uint16_t>(MessageType::RoomMembersResponse), frame.sequence, response.Serialize());
}

void HandleClient(socket_t sock, RoomRepository& repo) {
    Frame frame;
    if (ReceiveFrame(sock, frame) != FrameResult::Ok) {
        std::cout << "[room] Failed to receive frame" << std::endl;
        CloseSocket(sock);
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

    CloseSocket(sock);
}

int main() {
    AppConfig config;
    try {
        config = LoadConfig();
    }
    catch (const std::exception& ex) {
        std::cerr << "[room] Config error: " << ex.what() << std::endl;
        return 1;
    }

    SocketLibraryGuard socketLibrary;
    if (!socketLibrary.IsInitialized()) {
        std::cerr << "[room] Failed to initialize socket library" << std::endl;
        return 1;
    }

    std::unique_ptr<RoomRepository> repo;
    try {
        repo = std::make_unique<RoomRepository>(config.room.dbPath);
    }
    catch (const std::exception& ex) {
        std::cerr << "[room] Database error: " << ex.what() << std::endl;
        return 1;
    }
    std::cout << "[room] Database ready at " << config.room.dbPath << std::endl;

    socket_t listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(config.room.port);

    bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    std::cout << "[room] Listening on port " << config.room.port << std::endl;

    while (true) {
        socket_t clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == kInvalidSocket) continue;
        std::thread(HandleClient, clientSocket, std::ref(*repo)).detach();
    }

    CloseSocket(listenSocket);
    return 0;
}