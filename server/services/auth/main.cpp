#include <iostream>
#include <memory>
#include <thread>

#include "PlatformSocket.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "RoomMessages.h"
#include "RoleMessages.h"
#include "IpBanMessages.h"
#include "AvatarMessages.h"
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

void HandleDeleteUser(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = DeleteUserRequestPayload::Deserialize(frame.payload);
    std::cout << "[auth] Delete user id=" << request.userId << std::endl;
    StatusResponsePayload response;
    response.status = repo.DeleteUser(request.userId) ? 0 : 1; // 1 = не найден
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::DeleteUserResponse), frame.sequence, response.Serialize());
}

void HandleRoleList(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    RoleListResponsePayload response;
    for (const auto& role : repo.ListRoles()) {
        response.roles.push_back(RoleInfo{ role.id, role.name, role.isSystem, role.permissions, role.displayName });
    }
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::RoleListResponse), frame.sequence, response.Serialize());
}

void HandleUserList(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    UserListResponsePayload response;
    for (const auto& user : repo.ListUsers()) {
        response.users.push_back(UserInfo{ user.id, user.username, user.roleIds, false, user.avatarVersion, "" });
    }
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::UserListResponse), frame.sequence, response.Serialize());
}

// Аватарка не секрет — доступна любому залогиненному, тот же тип клиент->gateway
// (raw-forward) и gateway->auth, как список ролей/участников.
void HandleAvatarFetch(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = AvatarFetchRequestPayload::Deserialize(frame.payload);
    auto avatar = repo.GetAvatar(request.userId);
    AvatarFetchResponsePayload response;
    response.userId = request.userId;
    response.version = avatar.version;
    response.data = std::move(avatar.bytes);
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::AvatarFetchResponse), frame.sequence, response.Serialize());
}

// Internal only — userId уже подставлен и проверен gateway'ем (composite-обработчик
// там же), auth просто сохраняет байты и отдаёт новую версию.
void HandleSetUserAvatar(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = SetUserAvatarRequestPayload::Deserialize(frame.payload);
    int64_t version = repo.SetAvatar(request.userId, request.data);
    SetUserAvatarResponsePayload response;
    response.status = version < 0 ? 1 : 0;  // 1 = пользователь не найден
    response.version = version < 0 ? 0 : version;
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::SetUserAvatarResponse), frame.sequence, response.Serialize());
}

void HandleRoleCreate(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = RoleCreateRequestPayload::Deserialize(frame.payload);
    RoleCreateResponsePayload response;
    int64_t roleId = repo.CreateRole(request.name, request.permissions, request.displayName);
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
    switch (repo.UpdateRole(request.roleId, request.name, request.permissions, request.displayName)) {
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
    auto roleData = repo.GetUserRoleData(request.userId);
    MyPermissionsPayload response;
    response.permissions = roleData.permissions;
    response.roleIds = std::move(roleData.roleIds);
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::GetUserPermissionsResponse), frame.sequence, response.Serialize());
}

void HandleIsIpBanned(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = IpPayload::Deserialize(frame.payload);
    IpBanStatusPayload response;
    response.banned = repo.IsIpBanned(request.ip) ? 1 : 0;
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::IsIpBannedResponse), frame.sequence, response.Serialize());
}

void HandleIpBanList(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    IpBanListResponsePayload response;
    response.ips = repo.ListBannedIps();
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::IpBanListResponse), frame.sequence, response.Serialize());
}

void HandleIpBan(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = IpPayload::Deserialize(frame.payload);
    std::cout << "[auth] Ban IP " << request.ip << std::endl;
    repo.BanIp(request.ip);
    StatusResponsePayload response;
    response.status = 0;
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::IpBanResponse), frame.sequence, response.Serialize());
}

void HandleIpUnban(socket_t clientSocket, UserRepository& repo, const Frame& frame) {
    auto request = IpPayload::Deserialize(frame.payload);
    std::cout << "[auth] Unban IP " << request.ip << std::endl;
    repo.UnbanIp(request.ip);
    StatusResponsePayload response;
    response.status = 0;
    SendFrame(clientSocket, static_cast<uint16_t>(MessageType::IpUnbanResponse), frame.sequence, response.Serialize());
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
    case MessageType::DeleteUserRequest:        HandleDeleteUser(clientSocket, repo, frame); break;
    case MessageType::RoleListRequest:          HandleRoleList(clientSocket, repo, frame); break;
    case MessageType::UserListRequest:          HandleUserList(clientSocket, repo, frame); break;
    case MessageType::RoleCreateRequest:        HandleRoleCreate(clientSocket, repo, frame); break;
    case MessageType::RoleUpdateRequest:        HandleRoleUpdate(clientSocket, repo, frame); break;
    case MessageType::RoleDeleteRequest:        HandleRoleDelete(clientSocket, repo, frame); break;
    case MessageType::RoleAssignRequest:        HandleRoleAssign(clientSocket, repo, frame); break;
    case MessageType::RoleRemoveRequest:        HandleRoleRemove(clientSocket, repo, frame); break;
    case MessageType::GetUserPermissionsRequest: HandleGetUserPermissions(clientSocket, repo, frame); break;
    case MessageType::IsIpBannedRequest:        HandleIsIpBanned(clientSocket, repo, frame); break;
    case MessageType::IpBanListRequest:         HandleIpBanList(clientSocket, repo, frame); break;
    case MessageType::IpBanRequest:             HandleIpBan(clientSocket, repo, frame); break;
    case MessageType::IpUnbanRequest:           HandleIpUnban(clientSocket, repo, frame); break;
    case MessageType::AvatarFetchRequest:       HandleAvatarFetch(clientSocket, repo, frame); break;
    case MessageType::SetUserAvatarRequest:     HandleSetUserAvatar(clientSocket, repo, frame); break;
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
        repo = std::make_unique<UserRepository>(config.auth.dbPath, config.auth.avatarDir);
    }
    catch (const std::exception& ex) {
        std::cerr << "[auth] Database error: " << ex.what() << std::endl;
        return 1;
    }
    std::cout << "[auth] Database ready at " << config.auth.dbPath << std::endl;

    socket_t listenSocket = CreateListenSocket(config.auth.port);
    if (listenSocket == kInvalidSocket) {
        std::cerr << "[auth] Bind failed" << std::endl;
        return 1;
    }

    std::cout << "[auth] Listening on port " << config.auth.port << std::endl;

    // Поток на подключение — как у room_service/message_service/gateway. Раньше
    // auth_service обрабатывал соединения строго по одному в главном потоке; это
    // самый нагруженный внутренний сервис (логин с намеренно медленным PBKDF2,
    // да ещё и IsIpBannedRequest на КАЖДОЕ новое подключение к gateway), так что
    // один медленный вызов держал в очереди вообще всех остальных. UserRepository
    // теперь потокобезопасен (свой m_mutex, как у RoomRepository).
    while (true) {
        socket_t clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == kInvalidSocket) continue;
        std::thread(HandleClient, clientSocket, std::ref(*repo)).detach();
    }

    CloseSocket(listenSocket);
    return 0;
}