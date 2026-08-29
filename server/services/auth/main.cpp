#include <iostream>
#include <memory>

#include "PlatformSocket.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "RoomMessages.h"
#include "RoleMessages.h"
#include "UserRepository.h"
#include "PasswordHasher.h"
#include "Config.h"

void HandleRegister(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
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

void HandleLogin(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
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

void HandleRoleList(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    RoleListResponsePayload response;
    for (const auto& role : repo.ListRoles()) {
        response.roles.push_back(RoleInfo{ role.id, role.name, role.isSystem, role.permissions });
    }
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::RoleListResponse), frame.sequence, response.Serialize());
}

void HandleRoleCreate(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = RoleCreateRequestPayload::Deserialize(frame.payload);
    RoleCreateResponsePayload response;
    int64_t roleId = repo.CreateRole(request.name, request.permissions);
    if (roleId == -1) {
        response.status = 1; // имя занято
    }
    else {
        response.status = 0;
        response.roleId = roleId;
    }
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::RoleCreateResponse), frame.sequence, response.Serialize());
}

void HandleRoleUpdate(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = RoleUpdateRequestPayload::Deserialize(frame.payload);
    StatusResponsePayload response;
    switch (repo.UpdateRole(request.roleId, request.name, request.permissions)) {
    case RoleOpResult::Ok:         response.status = 0; break;
    case RoleOpResult::NotFound:   response.status = 1; break;
    case RoleOpResult::SystemRole: response.status = 2; break;
    case RoleOpResult::NameTaken:  response.status = 3; break;
    }
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::RoleUpdateResponse), frame.sequence, response.Serialize());
}

void HandleRoleDelete(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = RoleDeleteRequestPayload::Deserialize(frame.payload);
    StatusResponsePayload response;
    switch (repo.DeleteRole(request.roleId)) {
    case RoleOpResult::Ok:         response.status = 0; break;
    case RoleOpResult::NotFound:   response.status = 1; break;
    case RoleOpResult::SystemRole: response.status = 2; break;
    default:                       response.status = 1; break;
    }
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::RoleDeleteResponse), frame.sequence, response.Serialize());
}

void HandleRoleAssign(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = RoleMembershipRequestPayload::Deserialize(frame.payload);
    StatusResponsePayload response;
    response.status = repo.AssignRole(request.userId, request.roleId) ? 0 : 1; // 1 = уже назначена
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::RoleAssignResponse), frame.sequence, response.Serialize());
}

void HandleRoleRemove(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = RoleMembershipRequestPayload::Deserialize(frame.payload);
    StatusResponsePayload response;
    response.status = repo.RemoveRole(request.userId, request.roleId) ? 0 : 1; // 1 = не была назначена
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::RoleRemoveResponse), frame.sequence, response.Serialize());
}

void HandleGetUserPermissions(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = GetUserPermissionsRequestPayload::Deserialize(frame.payload);
    MyPermissionsPayload response;
    response.permissions = repo.GetUserPermissions(request.userId);
    response.roleIds = repo.GetUserRoleIds(request.userId);
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::GetUserPermissionsResponse), frame.sequence, response.Serialize());
}

void HandleClient(socket_t clientSocket, UserRepository& repo) {
    Frame frame;
    FrameResult result = ReceiveFrame(clientSocket, frame);

    if (result != FrameResult::Ok) {
        std::cout << "[auth] Failed to receive frame" << std::endl;
        CloseSocket(clientSocket);
        return;
    }

    switch (static_cast<MessageType>(frame.messageType)) {
    case MessageType::RegisterRequest:          HandleRegister(clientSocket, repo, frame); break;
    case MessageType::AuthRequest:              HandleLogin(clientSocket, repo, frame); break;
    case MessageType::RoleListRequest:          HandleRoleList(clientSocket, repo, frame); break;
    case MessageType::RoleCreateRequest:        HandleRoleCreate(clientSocket, repo, frame); break;
    case MessageType::RoleUpdateRequest:        HandleRoleUpdate(clientSocket, repo, frame); break;
    case MessageType::RoleDeleteRequest:        HandleRoleDelete(clientSocket, repo, frame); break;
    case MessageType::RoleAssignRequest:        HandleRoleAssign(clientSocket, repo, frame); break;
    case MessageType::RoleRemoveRequest:        HandleRoleRemove(clientSocket, repo, frame); break;
    case MessageType::GetUserPermissionsRequest: HandleGetUserPermissions(clientSocket, repo, frame); break;
    default:
        std::cout << "[auth] Unexpected messageType: " << frame.messageType << std::endl;
        break;
    }

    CloseSocket(clientSocket);
}

int main() {
    AppConfig config;
    try {
        config = LoadConfig();
    }
    catch (const std::exception& ex) {
        std::cerr << "[auth] Config error: " << ex.what() << std::endl;
        return 1;
    }

    SocketLibraryGuard socketLibrary;
    if (!socketLibrary.IsInitialized()) {
        std::cerr << "[auth] Failed to initialize socket library" << std::endl;
        return 1;
    }

    std::unique_ptr<UserRepository> repo;
    try {
        repo = std::make_unique<UserRepository>(config.auth.dbPath);
    }
    catch (const std::exception& ex) {
        std::cerr << "[auth] Database error: " << ex.what() << std::endl;
        return 1;
    }
    std::cout << "[auth] Database ready at " << config.auth.dbPath << std::endl;

    socket_t listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(config.auth.port);

    bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    std::cout << "[auth] Listening on port " << config.auth.port << std::endl;

    while (true) {
        socket_t clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == kInvalidSocket) continue;
        std::cout << "[auth] Client connected" << std::endl;
        HandleClient(clientSocket, *repo);
    }

    CloseSocket(listenSocket);
    return 0;
}