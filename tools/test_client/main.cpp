#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "WinsockGuard.h"
#include "TcpFramer.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "RoomMessages.h"

constexpr const char* SERVICE_HOST = "127.0.0.1";
constexpr int AUTH_SERVICE_PORT = 6001;
constexpr int ROOM_SERVICE_PORT = 6002;

// Текущий залогиненный пользователь. 0 = не залогинен.
int64_t g_currentUserId = 0;

SOCKET ConnectTo(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, SERVICE_HOST, &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

// Отправляет запрос, ждёт ответ. Возвращает false при любой сетевой ошибке.
bool Exchange(int port, MessageType requestType, const std::vector<uint8_t>& payload,
    MessageType expectedResponseType, Frame& outResponse) {
    SOCKET sock = ConnectTo(port);
    if (sock == INVALID_SOCKET) {
        std::cerr << "  [!] Failed to connect to port " << port << ". Is the service running?" << std::endl;
        return false;
    }

    if (SendFrame(sock, static_cast<uint16_t>(requestType), 1, payload) != FrameResult::Ok) {
        std::cerr << "  [!] Failed to send request" << std::endl;
        closesocket(sock);
        return false;
    }

    if (ReceiveFrame(sock, outResponse) != FrameResult::Ok) {
        std::cerr << "  [!] Failed to receive response" << std::endl;
        closesocket(sock);
        return false;
    }

    closesocket(sock);

    if (outResponse.messageType != static_cast<uint16_t>(expectedResponseType)) {
        std::cerr << "  [!] Unexpected messageType: " << outResponse.messageType << std::endl;
        return false;
    }
    return true;
}

// --- Auth ---

void DoAuth(MessageType requestType, MessageType responseType) {
    std::string username, password;
    std::cout << "  Username: "; std::cin >> username;
    std::cout << "  Password: "; std::cin >> password;

    AuthRequestPayload request;
    request.username = username;
    request.password = password;

    Frame response;
    if (!Exchange(AUTH_SERVICE_PORT, requestType, request.Serialize(), responseType, response)) return;

    auto payload = AuthResponsePayload::Deserialize(response.payload);
    std::cout << "  status = " << static_cast<int>(payload.status)
        << (payload.status == 0 ? " (success)" : " (failed)") << std::endl;
    std::cout << "  userId = " << payload.userId << std::endl;

    if (payload.status == 0 && requestType == MessageType::AuthRequest) {
        g_currentUserId = payload.userId;
        std::cout << "  [i] Logged in as userId=" << g_currentUserId << std::endl;
    }
}

// --- Rooms ---

void DoCreateRoom() {
    std::string name;
    std::cout << "  Room name: "; std::cin >> name;

    RoomCreateRequestPayload request;
    request.name = name;

    Frame response;
    if (!Exchange(ROOM_SERVICE_PORT, MessageType::RoomCreateRequest, request.Serialize(),
        MessageType::RoomCreateResponse, response)) return;

    auto payload = RoomCreateResponsePayload::Deserialize(response.payload);
    if (payload.status == 0) {
        std::cout << "  Room created, roomId = " << payload.roomId << std::endl;
    }
    else {
        std::cout << "  Failed: room name already taken" << std::endl;
    }
}

void DoListRooms() {
    Frame response;
    if (!Exchange(ROOM_SERVICE_PORT, MessageType::RoomListRequest, {},
        MessageType::RoomListResponse, response)) return;

    auto payload = RoomListResponsePayload::Deserialize(response.payload);
    if (payload.rooms.empty()) {
        std::cout << "  (no rooms yet)" << std::endl;
        return;
    }
    for (const auto& room : payload.rooms) {
        std::cout << "  [" << room.id << "] " << room.name << std::endl;
    }
}

void DoMembership(MessageType requestType, MessageType responseType, const char* actionName) {
    if (g_currentUserId == 0) {
        std::cout << "  [!] You must log in first (option 2)" << std::endl;
        return;
    }

    int64_t roomId = 0;
    std::cout << "  Room id: "; std::cin >> roomId;

    RoomMembershipRequestPayload request;
    request.roomId = roomId;
    request.userId = g_currentUserId;

    Frame response;
    if (!Exchange(ROOM_SERVICE_PORT, requestType, request.Serialize(), responseType, response)) return;

    auto payload = StatusResponsePayload::Deserialize(response.payload);
    switch (payload.status) {
    case 0: std::cout << "  " << actionName << " OK" << std::endl; break;
    case 1: std::cout << "  Failed: room not found" << std::endl; break;
    case 2: std::cout << "  Failed: membership state conflict (already member / not a member)" << std::endl; break;
    default: std::cout << "  Unknown status: " << static_cast<int>(payload.status) << std::endl; break;
    }
}

void DoListMembers() {
    int64_t roomId = 0;
    std::cout << "  Room id: "; std::cin >> roomId;

    RoomMembersRequestPayload request;
    request.roomId = roomId;

    Frame response;
    if (!Exchange(ROOM_SERVICE_PORT, MessageType::RoomMembersRequest, request.Serialize(),
        MessageType::RoomMembersResponse, response)) return;

    auto payload = RoomMembersResponsePayload::Deserialize(response.payload);
    if (payload.userIds.empty()) {
        std::cout << "  (no members)" << std::endl;
        return;
    }
    for (int64_t id : payload.userIds) {
        std::cout << "  userId = " << id << std::endl;
    }
}

int main() {
    WinsockGuard winsock;
    if (!winsock.IsInitialized()) {
        std::cerr << "[client] Failed to initialize Winsock" << std::endl;
        return 1;
    }

    while (true) {
        std::cout << "\n=== FreeCord test client ===" << std::endl;
        std::cout << "  current user: "
            << (g_currentUserId == 0 ? "(not logged in)" : std::to_string(g_currentUserId)) << std::endl;
        std::cout << "--- auth ---" << std::endl;
        std::cout << "1 - Register" << std::endl;
        std::cout << "2 - Login" << std::endl;
        std::cout << "--- rooms ---" << std::endl;
        std::cout << "3 - Create room" << std::endl;
        std::cout << "4 - List rooms" << std::endl;
        std::cout << "5 - Join room" << std::endl;
        std::cout << "6 - Leave room" << std::endl;
        std::cout << "7 - List members" << std::endl;
        std::cout << "0 - Exit" << std::endl;
        std::cout << "> ";

        int choice = 0;
        if (!(std::cin >> choice)) break;

        switch (choice) {
        case 0: std::cout << "[client] Bye" << std::endl; return 0;
        case 1: DoAuth(MessageType::RegisterRequest, MessageType::RegisterResponse); break;
        case 2: DoAuth(MessageType::AuthRequest, MessageType::AuthResponse); break;
        case 3: DoCreateRoom(); break;
        case 4: DoListRooms(); break;
        case 5: DoMembership(MessageType::RoomJoinRequest, MessageType::RoomJoinResponse, "Joined"); break;
        case 6: DoMembership(MessageType::RoomLeaveRequest, MessageType::RoomLeaveResponse, "Left"); break;
        case 7: DoListMembers(); break;
        default: std::cout << "  Unknown option" << std::endl; break;
        }
    }

    return 0;
}