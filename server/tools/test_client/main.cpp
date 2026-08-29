#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

#include "PlatformSocket.h"
#include "TcpFramer.h"
#include "TlsTransport.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "RoomMessages.h"
#include "MessageMessages.h"
#include "ChatMessages.h"

constexpr const char* GATEWAY_HOST = "127.0.0.1";
constexpr int GATEWAY_PORT = 6000;

socket_t g_socket = kInvalidSocket;
std::shared_ptr<ITransport> g_transport;   // TLS-соединение с gateway
std::mutex g_sendMutex;          // клиент тоже пишет из разных мест — защищаем отправку
std::atomic<bool> g_running{ true };
std::atomic<int64_t> g_userId{ 0 };

bool Send(MessageType type, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(g_sendMutex);
    return SendFrame(*g_transport, static_cast<uint16_t>(type), 0, payload) == FrameResult::Ok;
}

// --- Обработка входящих кадров (работает в отдельном потоке) ---

void HandleFrame(const Frame& frame) {
    switch (static_cast<MessageType>(frame.messageType)) {
    case MessageType::AuthResponse: {
        auto p = AuthResponsePayload::Deserialize(frame.payload);
        if (p.status == 0) {
            g_userId = p.userId;
            std::cout << "\n  [+] Logged in as userId=" << p.userId << std::endl;
        }
        else {
            std::cout << "\n  [!] Login failed (status=" << static_cast<int>(p.status) << ")" << std::endl;
        }
        break;
    }
    case MessageType::RegisterResponse: {
        auto p = AuthResponsePayload::Deserialize(frame.payload);
        std::cout << "\n  " << (p.status == 0
            ? "[+] Registered, userId=" + std::to_string(p.userId)
            : "[!] Registration failed (username taken?)") << std::endl;
        break;
    }
    case MessageType::RoomCreateResponse: {
        auto p = RoomCreateResponsePayload::Deserialize(frame.payload);
        std::cout << "\n  " << (p.status == 0
            ? "[+] Room created, roomId=" + std::to_string(p.roomId)
            : "[!] Room name taken") << std::endl;
        break;
    }
    case MessageType::RoomListResponse: {
        auto p = RoomListResponsePayload::Deserialize(frame.payload);
        std::cout << "\n  --- rooms ---" << std::endl;
        if (p.rooms.empty()) std::cout << "  (none)" << std::endl;
        for (const auto& r : p.rooms) {
            std::cout << "  [" << r.id << "] " << r.name << std::endl;
        }
        break;
    }
    case MessageType::JoinRoomResponse:
    case MessageType::RoomLeaveResponse: {
        auto p = StatusResponsePayload::Deserialize(frame.payload);
        const char* msg[] = { "[+] OK", "[!] Room not found", "[!] Membership conflict" };
        std::cout << "\n  " << (p.status < 3 ? msg[p.status] : "[!] Error") << std::endl;
        break;
    }
    case MessageType::HistoryResponse: {
        auto p = HistoryResponsePayload::Deserialize(frame.payload);
        std::cout << "\n  --- history ---" << std::endl;
        if (p.messages.empty()) std::cout << "  (empty)" << std::endl;
        for (const auto& m : p.messages) {
            std::cout << "  [" << m.id << "] user" << m.senderId << ": " << m.text << std::endl;
        }
        break;
    }
    case MessageType::TextMessage: {
        auto p = BroadcastTextMessagePayload::Deserialize(frame.payload);
        std::cout << "\n  <room " << p.roomId << "> " << p.senderName << ": " << p.text << std::endl;
        std::cout << "> " << std::flush;   // возвращаем приглашение, его затёрло выводом
        break;
    }
    case MessageType::Pong:
        std::cout << "\n  [+] Pong" << std::endl;
        break;
    case MessageType::UserJoined:
    case MessageType::UserLeft: {
        auto p = UserPresencePayload::Deserialize(frame.payload);
        std::cout << "\n  <room " << p.roomId << "> " << p.username
            << (frame.messageType == static_cast<uint16_t>(MessageType::UserJoined)
                ? " joined" : " left") << std::endl;
        std::cout << "> " << std::flush;
        break;
    }
    default:
        std::cout << "\n  [?] Unhandled messageType 0x" << std::hex
            << frame.messageType << std::dec << std::endl;
        break;
    }
}

void ReceiveThread() {
    while (g_running) {
        Frame frame;
        FrameResult result = ReceiveFrame(*g_transport, frame);
        if (result != FrameResult::Ok) {
            if (g_running) std::cout << "\n  [!] Connection lost" << std::endl;
            g_running = false;
            break;
        }
        HandleFrame(frame);
    }
}

// --- Команды пользователя ---

void DoAuth(bool isRegister) {
    AuthRequestPayload p;
    std::cout << "  Username: "; std::cin >> p.username;
    std::cout << "  Password: "; std::cin >> p.password;
    Send(isRegister ? MessageType::RegisterRequest : MessageType::AuthRequest, p.Serialize());
}

void DoCreateRoom() {
    RoomCreateRequestPayload p;
    std::cout << "  Room name: "; std::cin >> p.name;
    Send(MessageType::RoomCreateRequest, p.Serialize());
}

void DoMembership(bool isJoin) {
    RoomMembershipRequestPayload p;
    std::cout << "  Room id: "; std::cin >> p.roomId;
    p.userId = 0;   // gateway подставит из сессии
    Send(isJoin ? MessageType::JoinRoom : MessageType::LeaveRoom, p.Serialize());
}

void DoSendMessage() {
    ClientTextMessagePayload p;
    std::cout << "  Room id: "; std::cin >> p.roomId;
    std::cin.ignore();
    std::cout << "  Text: ";
    std::getline(std::cin, p.text);
    Send(MessageType::TextMessage, p.Serialize());
}

void DoHistory() {
    HistoryRequestPayload p;
    std::cout << "  Room id: "; std::cin >> p.roomId;
    p.limit = 50;
    Send(MessageType::HistoryRequest, p.Serialize());
}

int main() {
    SocketLibraryGuard socketLibrary;
    if (!socketLibrary.IsInitialized()) {
        std::cerr << "[client] Socket library init failed" << std::endl;
        return 1;
    }

    g_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(GATEWAY_PORT);
    inet_pton(AF_INET, GATEWAY_HOST, &addr.sin_addr);

    if (connect(g_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::cerr << "[client] Cannot connect to gateway on port " << GATEWAY_PORT << std::endl;
        return 1;
    }

    std::string fingerprint;
    g_transport = TlsTransport::ConnectClientNoVerify(g_socket, fingerprint);
    if (!g_transport) {
        std::cerr << "[client] TLS handshake with gateway failed" << std::endl;
        CloseSocket(g_socket);
        return 1;
    }
    std::cout << "[client] Connected to gateway (TLS)" << std::endl;
    std::cout << "[client] Server certificate fingerprint (SHA-256): " << fingerprint << std::endl;
    std::cout << "[client] NOTE: this debug client does not verify the fingerprint against anything (no pinning)." << std::endl;

    std::thread receiver(ReceiveThread);

    while (g_running) {
        std::cout << "\n=== FreeCord ===  user: "
            << (g_userId == 0 ? "(not logged in)" : std::to_string(g_userId)) << std::endl;
        std::cout << "1-Register  2-Login  3-Create room  4-List rooms" << std::endl;
        std::cout << "5-Join  6-Leave  7-Send message  8-History  9-Ping  0-Exit" << std::endl;
        std::cout << "> ";

        int choice = 0;
        if (!(std::cin >> choice)) break;

        switch (choice) {
        case 0: g_running = false; break;
        case 1: DoAuth(true);       break;
        case 2: DoAuth(false);      break;
        case 3: DoCreateRoom();     break;
        case 4: Send(MessageType::RoomListRequest, {}); break;
        case 5: DoMembership(true);  break;
        case 6: DoMembership(false); break;
        case 7: DoSendMessage();    break;
        case 8: DoHistory();        break;
        case 9: Send(MessageType::Ping, {}); break;
        default: std::cout << "  Unknown option" << std::endl; break;
        }

        // Даём потоку-приёмнику успеть напечатать ответ до перерисовки меню.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    g_running = false;
    CloseSocket(g_socket);
    receiver.join();
    std::cout << "[client] Bye" << std::endl;
    return 0;
}