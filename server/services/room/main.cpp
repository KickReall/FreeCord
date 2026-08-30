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
        response.rooms.push_back(RoomInfo{ record.id, record.name, static_cast<RoomType>(record.type) });
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

void HandleGetChannelOverrides(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = ChannelOverridesRequestPayload::Deserialize(frame.payload);
    ChannelOverridesResponsePayload response;
    for (const auto& o : repo.GetChannelOverrides(request.roomId)) {
        response.overrides.push_back(ChannelOverrideInfo{ o.roleId, o.allow, o.deny });
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::ChannelOverridesResponse), frame.sequence, response.Serialize());
}

void HandleSetChannelOverride(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = SetChannelOverrideRequestPayload::Deserialize(frame.payload);
    StatusResponsePayload response;
    if (!repo.RoomExists(request.roomId)) {
        response.status = 1; // room not found
    }
    else {
        repo.SetChannelOverride(request.roomId, request.roleId, request.allow, request.deny);
        response.status = 0;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::SetChannelOverrideResponse), frame.sequence, response.Serialize());
}

void HandleDeleteChannelOverride(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = DeleteChannelOverrideRequestPayload::Deserialize(frame.payload);
    StatusResponsePayload response;
    if (!repo.RoomExists(request.roomId)) {
        response.status = 1; // room not found
    }
    else {
        repo.DeleteChannelOverride(request.roomId, request.roleId);
        response.status = 0;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::DeleteChannelOverrideResponse), frame.sequence, response.Serialize());
}

void HandleChannelModerationStatus(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    ChannelModerationStatusResponsePayload response;
    response.banned = repo.IsBanned(request.roomId, request.userId) ? 1 : 0;
    response.muted = repo.IsMuted(request.roomId, request.userId) ? 1 : 0;
    SendFrame(sock, static_cast<uint16_t>(MessageType::ChannelModerationStatusResponse), frame.sequence, response.Serialize());
}

void HandleChannelKick(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    std::cout << "[room] Kick roomId=" << request.roomId << " userId=" << request.userId << std::endl;

    StatusResponsePayload response;
    if (!repo.RoomExists(request.roomId)) {
        response.status = 1; // room not found
    }
    else {
        repo.BanUser(request.roomId, request.userId);
        response.status = 0;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::ChannelKickResponse), frame.sequence, response.Serialize());
}

void HandleChannelUnban(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    StatusResponsePayload response;
    if (!repo.RoomExists(request.roomId)) {
        response.status = 1; // room not found
    }
    else {
        repo.UnbanUser(request.roomId, request.userId);
        response.status = 0;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::ChannelUnbanResponse), frame.sequence, response.Serialize());
}

void HandleChannelMute(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    std::cout << "[room] Mute roomId=" << request.roomId << " userId=" << request.userId << std::endl;

    StatusResponsePayload response;
    if (!repo.RoomExists(request.roomId)) {
        response.status = 1; // room not found
    }
    else {
        repo.MuteUser(request.roomId, request.userId);
        response.status = 0;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::ChannelMuteResponse), frame.sequence, response.Serialize());
}

void HandleChannelUnmute(socket_t sock, RoomRepository& repo, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    StatusResponsePayload response;
    if (!repo.RoomExists(request.roomId)) {
        response.status = 1; // room not found
    }
    else {
        repo.UnmuteUser(request.roomId, request.userId);
        response.status = 0;
    }
    SendFrame(sock, static_cast<uint16_t>(MessageType::ChannelUnmuteResponse), frame.sequence, response.Serialize());
}

void HandleClient(socket_t sock, RoomRepository& repo) {
    Frame frame;
    if (ReceiveFrame(sock, frame) != FrameResult::Ok) {
        std::cout << "[room] Failed to receive frame" << std::endl;
        CloseSocket(sock);
        return;
    }

    switch (static_cast<MessageType>(frame.messageType)) {
    case MessageType::RoomCreateRequest:            HandleCreate(sock, repo, frame);  break;
    case MessageType::RoomJoinRequest:               HandleJoin(sock, repo, frame);    break;
    case MessageType::RoomLeaveRequest:              HandleLeave(sock, repo, frame);   break;
    case MessageType::RoomListRequest:               HandleList(sock, repo, frame);    break;
    case MessageType::RoomMembersRequest:            HandleMembers(sock, repo, frame); break;
    case MessageType::ChannelOverridesRequest:       HandleGetChannelOverrides(sock, repo, frame);    break;
    case MessageType::SetChannelOverrideRequest:     HandleSetChannelOverride(sock, repo, frame);     break;
    case MessageType::DeleteChannelOverrideRequest:  HandleDeleteChannelOverride(sock, repo, frame);  break;
    case MessageType::ChannelModerationStatusRequest: HandleChannelModerationStatus(sock, repo, frame); break;
    case MessageType::ChannelKickRequest:            HandleChannelKick(sock, repo, frame);    break;
    case MessageType::ChannelUnbanRequest:           HandleChannelUnban(sock, repo, frame);   break;
    case MessageType::ChannelMuteRequest:            HandleChannelMute(sock, repo, frame);    break;
    case MessageType::ChannelUnmuteRequest:          HandleChannelUnmute(sock, repo, frame);  break;
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

    socket_t listenSocket = CreateListenSocket(config.room.port);
    if (listenSocket == kInvalidSocket) {
        std::cerr << "[room] Bind failed" << std::endl;
        return 1;
    }

    std::cout << "[room] Listening on port " << config.room.port << std::endl;

    while (true) {
        socket_t clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == kInvalidSocket) continue;
        std::thread(HandleClient, clientSocket, std::ref(*repo)).detach();
    }

    CloseSocket(listenSocket);
    return 0;
}