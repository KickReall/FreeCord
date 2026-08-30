#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <memory>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "PlatformSocket.h"
#include "TcpFramer.h"
#include "TlsContext.h"
#include "TlsTransport.h"
#include "ProtocolTypes.h"
#include "AuthMessages.h"
#include "RoomMessages.h"
#include "MessageMessages.h"
#include "ChatMessages.h"
#include "RoleMessages.h"
#include "IpBanMessages.h"
#include "AvatarMessages.h"
#include "Permissions.h"
#include "ServiceClient.h"
#include "SessionManager.h"
#include "Config.h"

// Внутренний инвариант, завязанный на seed-данные в db/room/migrations/001_initial.sql —
// не выносим в config.json, иначе конфиг и БД могут разойтись.
constexpr int64_t SYSTEM_ROOM_ID = 1;

SessionManager g_sessions;
AppConfig g_config;
std::unique_ptr<TlsContext> g_tls;

// Иконка сервера — один файл на весь деплой (у gateway нет своей БД, как у
// gateway.crt/.key). version=0 значит "иконки нет"; хранится отдельным текстовым
// файлом рядом с картинкой, чтобы не терять счётчик при перезапуске (иначе клиенты
// после каждого рестарта сервиса решали бы, что иконка "изменилась", и качали
// её заново, хотя файл на диске тот же самый).
struct ServerIconState {
    int64_t version = 0;
    std::vector<uint8_t> bytes;
};
ServerIconState g_serverIcon;

std::string ServerIconVersionPath() {
    return g_config.gateway.serverIconPath + ".version";
}

void LoadServerIconAtStartup() {
    std::ifstream file(g_config.gateway.serverIconPath, std::ios::binary);
    if (!file) return;  // иконки ещё нет — version остаётся 0
    g_serverIcon.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    std::ifstream versionFile(ServerIconVersionPath());
    if (versionFile) versionFile >> g_serverIcon.version;
    else g_serverIcon.version = 1;  // файл иконки есть, а версии почему-то нет
}

bool SaveServerIcon(const std::vector<uint8_t>& bytes) {
    std::ofstream file(g_config.gateway.serverIconPath, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();

    g_serverIcon.bytes = bytes;
    g_serverIcon.version += 1;

    std::ofstream versionFile(ServerIconVersionPath(), std::ios::trunc);
    versionFile << g_serverIcon.version;
    return true;
}

// Имя и описание сервера — тоже общий на весь деплой файл рядом с иконкой, без
// версии (короткие строки, кэш не нужен, см. AvatarMessages.h). Пустые значения —
// валидное состояние "владелец ещё не задал", а не ошибка.
constexpr size_t kServerNameMaxLength = 100;
constexpr size_t kServerDescriptionMaxLength = 1000;

struct ServerInfoState {
    std::string name;
    std::string description;
};
ServerInfoState g_serverInfo;

std::string ServerInfoPath() {
    return "server-info.json";
}

void LoadServerInfoAtStartup() {
    std::ifstream file(ServerInfoPath());
    if (!file) return;  // ещё не задавали — имя/описание остаются пустыми
    try {
        nlohmann::json json;
        file >> json;
        g_serverInfo.name = json.value("name", "");
        g_serverInfo.description = json.value("description", "");
    }
    catch (const nlohmann::json::exception&) {
        std::cerr << "[gateway] Failed to parse " << ServerInfoPath() << ", ignoring" << std::endl;
    }
}

bool SaveServerInfo(const std::string& name, const std::string& description) {
    std::ofstream file(ServerInfoPath(), std::ios::trunc);
    if (!file) return false;
    nlohmann::json json;
    json["name"] = name;
    json["description"] = description;
    file << json.dump(2);
    if (!file) return false;

    g_serverInfo.name = name;
    g_serverInfo.description = description;
    return true;
}

struct ClientContext {
    socket_t socket = kInvalidSocket;         // нужен для SetRecvTimeout/CloseSocket — TLS работает поверх него
    std::string remoteIp;                     // адрес клиента, захваченный на accept() — для бана по IP
    std::shared_ptr<ITransport> transport;    // TLS-соединение с клиентом; до успешного handshake равен nullptr
    SessionPtr session;   // nullptr, пока не залогинен
};

// Потокобезопасная отправка в сессию.
bool SendToSession(const SessionPtr& session, MessageType type, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(session->sendMutex);
    return SendFrame(*session->transport, static_cast<uint16_t>(type), 0, payload) == FrameResult::Ok;
}

bool HasPermission(const SessionPtr& session, Permission permission) {
    return (session->permissions.load() & static_cast<uint32_t>(permission)) != 0;
}

// Обёртки над CallService для каждого внутреннего сервиса — раньше host+порт+таймаут
// повторялись в каждом вызове по всему файлу; теперь один источник правды на сервис.
bool CallAuth(MessageType requestType, const std::vector<uint8_t>& payload, MessageType responseType, Frame& outResponse) {
    return CallService(g_config.gateway.serviceHost.c_str(), g_config.auth.port, requestType, payload,
        responseType, outResponse, g_config.gateway.serviceCallTimeoutMs);
}
bool CallRoom(MessageType requestType, const std::vector<uint8_t>& payload, MessageType responseType, Frame& outResponse) {
    return CallService(g_config.gateway.serviceHost.c_str(), g_config.room.port, requestType, payload,
        responseType, outResponse, g_config.gateway.serviceCallTimeoutMs);
}
bool CallMessage(MessageType requestType, const std::vector<uint8_t>& payload, MessageType responseType, Frame& outResponse) {
    return CallService(g_config.gateway.serviceHost.c_str(), g_config.message.port, requestType, payload,
        responseType, outResponse, g_config.gateway.serviceCallTimeoutMs);
}

// Общий паттерн большинства Handle*: не хватает прав — синтезируем "отказано" самостоятельно,
// не трогая сервис; иначе форвардим frame.payload как есть и пересылаем ответ сервиса клиенту
// без разбора. Возвращает сырой ответ сервиса (пустой Frame — если прав не хватило или сервис
// недоступен), чтобы вызывающая сторона могла доразобрать его для доп. действий (кик, бан по IP).
template <typename CallFn, typename ForbiddenPayload>
Frame ProxyToService(ClientContext& ctx, const Frame& frame, Permission permission, CallFn call,
    MessageType requestType, MessageType responseType, const ForbiddenPayload& forbidden) {
    if (!HasPermission(ctx.session, permission)) {
        SendToSession(ctx.session, responseType, forbidden.Serialize());
        return {};
    }
    Frame response;
    if (!call(requestType, frame.payload, responseType, response)) {
        return {};
    }
    SendToSession(ctx.session, responseType, response.payload);
    return response;
}

// Проверяется до TLS-хендшейка — забаненный IP не должен тратить ресурсы даже на него.
bool IsIpBanned(const std::string& ip) {
    IpPayload request;
    request.ip = ip;

    Frame response;
    if (!CallAuth(MessageType::IsIpBannedRequest, request.Serialize(), MessageType::IsIpBannedResponse, response)) {
        return false; // auth_service недоступен — не блокируем на ровном месте
    }
    return IpBanStatusPayload::Deserialize(response.payload).banned != 0;
}

// Немедленно обрывает все текущие сессии с этого IP (по требованию — бан должен
// действовать сразу, а не только на будущие попытки подключиться). ShutdownSocket
// лишь прерывает блокирующий recv() в потоке-владельце сессии — тот сам довершит
// очистку (RemoveSession, CloseSocket) обычным путём, как при любом разрыве связи.
void DisconnectSessionsForIp(const std::string& ip) {
    for (const auto& session : g_sessions.GetSessionsForIp(ip)) {
        ShutdownSocket(session->rawSocket);
    }
}

// Разослать всем залогиненным клиентам, независимо от комнат.
int BroadcastToAll(MessageType type, const std::vector<uint8_t>& payload, int64_t excludeUserId = 0) {
    int delivered = 0;
    for (const auto& session : g_sessions.GetAllSessions()) {
        if (session->userId == excludeUserId) continue;
        if (SendToSession(session, type, payload)) delivered++;
    }
    return delivered;
}

// Разослать всем, кто сейчас открыл эту комнату.
int BroadcastToRoom(int64_t roomId, MessageType type, const std::vector<uint8_t>& payload, int64_t excludeUserId = 0) {
    int delivered = 0;
    for (const auto& session : g_sessions.GetSessionsInRoom(roomId)) {
        if (session->userId == excludeUserId) continue;
        if (SendToSession(session, type, payload)) delivered++;
    }
    return delivered;
}

// Эффективные права пользователя в конкретной комнате: базовые права сессии плюс
// оверрайды по её ролям (allow/deny из channel_role_overrides). Admin (все биты
// в session->permissions) оверрайды не затрагивают — иначе оверрайд смог бы
// отобрать права у суперпользователя, что ломает саму идею admin'а.
uint32_t EffectivePermissionsInRoom(const SessionPtr& session, int64_t roomId) {
    uint32_t base = session->permissions.load();
    if (base == 0xFFFFFFFFu) return base; // admin (и owner — он всегда ещё и admin) — суперпользователь, дальше не идём

    uint32_t effective = base;

    Frame overridesResponse;
    ChannelOverridesRequestPayload request;
    request.roomId = roomId;
    if (CallRoom(MessageType::ChannelOverridesRequest, request.Serialize(), MessageType::ChannelOverridesResponse, overridesResponse)) {
        auto overrides = ChannelOverridesResponsePayload::Deserialize(overridesResponse.payload);

        uint32_t allow = 0, deny = 0;
        for (const auto& o : overrides.overrides) {
            bool hasRole = std::find(session->roleIds.begin(), session->roleIds.end(), o.roleId) != session->roleIds.end();
            if (!hasRole) continue;
            allow |= o.allow;
            deny |= o.deny;
        }

        // Явный allow хоть у одной роли побеждает deny у другой — ролевой иерархии
        // (как у Discord) в проекте нет, поэтому используем самое простое правило.
        effective = (base & ~deny) | allow;
    }
    // иначе room_service недоступен — не блокируем на ровном месте, работаем с базовыми правами

    // Системная комната — писать в неё может только суперпользователь (admin/owner,
    // они уже отсеклись сентинелом выше). Жёсткое правило поверх оверрайдов, а не
    // просто deny в БД: администратор мог бы случайно выдать сюда allow через вкладку
    // "Каналы", и это не должно суметь пробить запрет — видимость и вход не трогаем,
    // системную комнату по-прежнему видят и открывают все.
    if (roomId == SYSTEM_ROOM_ID) {
        effective &= ~static_cast<uint32_t>(Permission::SendMessages);
    }

    return effective;
}

// Статус модерации пользователя в комнате (бан/мут). Как и оверрайды, для admin
// (сентинел 0xFFFFFFFF в session->permissions) не проверяется вовсе — иначе
// любая роль с KickMembers/ManageChannelModeration могла бы запереть суперпользователя.
ChannelModerationStatusResponsePayload GetModerationStatus(const SessionPtr& session, int64_t roomId) {
    ChannelModerationStatusResponsePayload result;
    if (session->permissions.load() == 0xFFFFFFFFu) return result;

    RoomMembershipRequestPayload request;
    request.roomId = roomId;
    request.userId = session->userId;

    Frame response;
    if (!CallRoom(MessageType::ChannelModerationStatusRequest, request.Serialize(), MessageType::ChannelModerationStatusResponse, response)) {
        return result; // room_service недоступен — не блокируем на ровном месте
    }
    return ChannelModerationStatusResponsePayload::Deserialize(response.payload);
}

// Владелец (kOwnerRoleId) неприкосновенен для любого действия, нацеленного на него
// извне — даже от admin. Проверяем через GetUserPermissionsRequest (тот же internal
// вызов, что и при логине), а не через кэш сессии инициатора: цель действия обычно
// не совпадает с сессией, которая его выполняет, и может быть офлайн в момент проверки.
bool IsTargetOwner(int64_t userId) {
    GetUserPermissionsRequestPayload request;
    request.userId = userId;
    Frame response;
    if (!CallAuth(MessageType::GetUserPermissionsRequest, request.Serialize(), MessageType::GetUserPermissionsResponse, response)) {
        return false; // auth_service недоступен — не блокируем на ровном месте
    }
    auto perms = MyPermissionsPayload::Deserialize(response.payload);
    return std::find(perms.roleIds.begin(), perms.roleIds.end(), kOwnerRoleId) != perms.roleIds.end();
}

// Права кэшируются в сессии на момент логина и обычно не пересчитываются заново
// (см. Session::permissions) — но после изменения чужих ролей через RoleAssign/
// RoleRemove это молчаливое устаревание было бы прямо видно (пользователю с новой
// ролью admin пришлось бы перезайти). Поэтому именно эти два места принудительно
// обновляют кэш и досылают MyPermissions, если пользователь сейчас онлайн.
void RefreshPermissionsIfOnline(int64_t userId) {
    auto sessions = g_sessions.GetSessionsForUsers({ userId });
    if (sessions.empty()) return;

    GetUserPermissionsRequestPayload request;
    request.userId = userId;
    Frame response;
    if (!CallAuth(MessageType::GetUserPermissionsRequest, request.Serialize(), MessageType::GetUserPermissionsResponse, response)) {
        return;
    }
    auto perms = MyPermissionsPayload::Deserialize(response.payload);
    for (const auto& session : sessions) {
        session->permissions.store(perms.permissions);
        session->roleIds = perms.roleIds;
        SendToSession(session, MessageType::MyPermissions, perms.Serialize());
    }
}

// auth ничего не знает о живых TCP-сессиях (у него только БД) — online проставляет
// только gateway, сверяясь с SessionManager. false при недоступном auth — как и
// раньше у HandleUserList, молча не отвечаем, а не подсовываем пустой список.
bool FetchUserListWithPresence(UserListResponsePayload& outResult) {
    Frame userResponse;
    if (!CallAuth(MessageType::UserListRequest, {}, MessageType::UserListResponse, userResponse)) {
        return false;
    }
    outResult = UserListResponsePayload::Deserialize(userResponse.payload);
    for (auto& user : outResult.users) {
        auto sessions = g_sessions.GetSessionsForUsers({ user.id });
        user.online = !sessions.empty();
        // Несколько параллельных сессий одного пользователя — редкость (второй клиент
        // тем же логином), но теоретически возможна; берём IP первой попавшейся, для
        // отображения в панели администратора этого достаточно.
        user.ip = sessions.empty() ? "" : sessions.front()->remoteIp;
    }
    return true;
}

// Досылает актуальный список пользователей всем залогиненным клиентам — используется
// после логина/дисконнекта (поменялась online) и после удаления аккаунта (пользователь
// должен пропасть из панели участников у всех, не только у того, кто его удалил).
void BroadcastUserListToAll() {
    UserListResponsePayload result;
    if (!FetchUserListWithPresence(result)) return;
    BroadcastToAll(MessageType::UserListResponse, result.Serialize());
}

// Если пользователь сейчас онлайн и держит открытой эту комнату — выкинуть его
// немедленно (используется после кика, чтобы бан подействовал сразу, а не только
// при следующей попытке зайти).
void ForceLeaveRoomIfOnline(int64_t userId, int64_t roomId) {
    for (const auto& session : g_sessions.GetSessionsForUsers({ userId })) {
        int64_t expected = roomId;
        if (session->currentRoomId.compare_exchange_strong(expected, 0)) {
            RoomMembersRequestPayload notice;
            notice.roomId = roomId;
            SendToSession(session, MessageType::ChannelKicked, notice.Serialize());
        }
    }
}

// Как ForceLeaveRoomIfOnline, но для всех, кто сейчас держит канал открытым —
// используется при удалении канала целиком, а не при кике одного пользователя.
void ForceLeaveRoomForAll(int64_t roomId) {
    for (const auto& session : g_sessions.GetSessionsInRoom(roomId)) {
        int64_t expected = roomId;
        if (session->currentRoomId.compare_exchange_strong(expected, 0)) {
            RoomMembersRequestPayload notice;
            notice.roomId = roomId;
            SendToSession(session, MessageType::ChannelKicked, notice.Serialize());
        }
    }
}

// Token bucket: capacity — сколько запросов можно накопить про запас (короткий
// всплеск не режется), perSecond — скорость восполнения. Обновляет tokens/lastRefill
// на месте и возвращает, можно ли пропустить текущий запрос. Вызывается только из
// потока-владельца сессии (см. поля messageTokens/typingTokens в Session), поэтому
// без блокировок.
bool TryConsumeRateLimitToken(double& tokens, std::chrono::steady_clock::time_point& lastRefill,
    double capacity, double perSecond) {
    auto now = std::chrono::steady_clock::now();
    double elapsedSeconds = std::chrono::duration<double>(now - lastRefill).count();
    lastRefill = now;
    // Доп. скобки вокруг std::min — на Windows PlatformSocket.h тянет winsock2.h/windows.h
    // без NOMINMAX, а те определяют min/max как макросы; (std::min) не даёт препроцессору
    // принять голый "min(" за вызов макроса.
    tokens = (std::min)(capacity, tokens + elapsedSeconds * perSecond);
    if (tokens < 1.0) return false;
    tokens -= 1.0;
    return true;
}

void HandleAuth(ClientContext& ctx, const Frame& frame, bool isRegister) {
    auto request = AuthRequestPayload::Deserialize(frame.payload);

    Frame authResponse;
    bool ok = CallAuth(
        isRegister ? MessageType::RegisterRequest : MessageType::AuthRequest,
        frame.payload,
        isRegister ? MessageType::RegisterResponse : MessageType::AuthResponse,
        authResponse);

    AuthResponsePayload response;
    if (!ok) {
        response.status = 9;
        std::cout << "[gateway] auth_service unavailable" << std::endl;
    }
    else {
        response = AuthResponsePayload::Deserialize(authResponse.payload);

        if (response.status == 0 && !isRegister) {
            ctx.session = g_sessions.AddSession(response.userId, request.username, ctx.transport, ctx.remoteIp, ctx.socket);
            response.sessionId = ctx.session->sessionId;
            std::cout << "[gateway] '" << request.username << "' logged in (userId="
                << response.userId << "), online=" << g_sessions.OnlineCount() << std::endl;

            // Права считаются при логине и кэшируются в сессии — но не намертво:
            // RoleAssign/RoleRemove пересчитывают их принудительно через
            // RefreshPermissionsIfOnline, если целевой пользователь сейчас в сети.
            Frame permResponse;
            GetUserPermissionsRequestPayload permRequest;
            permRequest.userId = response.userId;
            if (CallAuth(MessageType::GetUserPermissionsRequest, permRequest.Serialize(),
                MessageType::GetUserPermissionsResponse, permResponse)) {
                auto perms = MyPermissionsPayload::Deserialize(permResponse.payload);
                ctx.session->permissions.store(perms.permissions);
                ctx.session->roleIds = perms.roleIds;
                SendToSession(ctx.session, MessageType::MyPermissions, perms.Serialize());
            }

            BroadcastUserListToAll(); // presence поменялась — досылаем всем свежий список
        }

        // Новый пользователь — пишем в системную комнату и уведомляем всех
        if (response.status == 0 && isRegister) {
            std::string text = "Новый пользователь: " + request.username;

            SendMessageRequestPayload sysMessage;
            sysMessage.roomId = SYSTEM_ROOM_ID;
            sysMessage.senderId = 0;
            sysMessage.senderName = "System";
            sysMessage.text = text;

            Frame saveResponse;
            if (CallMessage(MessageType::SendMessageRequest, sysMessage.Serialize(),
                MessageType::SendMessageResponse, saveResponse)) {

                auto saved = SendMessageResponsePayload::Deserialize(saveResponse.payload);
                if (saved.status == 0) {
                    BroadcastTextMessagePayload broadcast;
                    broadcast.messageId = saved.messageId;
                    broadcast.roomId = SYSTEM_ROOM_ID;
                    broadcast.senderId = 0;
                    broadcast.senderName = "System";
                    broadcast.timestamp = saved.timestamp;
                    broadcast.text = text;

                    BroadcastToRoom(SYSTEM_ROOM_ID, MessageType::TextMessage, broadcast.Serialize());
                }
            }

            UserRegisteredPayload notification;
            notification.userId = response.userId;
            notification.username = request.username;
            BroadcastToAll(MessageType::UserRegistered, notification.Serialize());

            std::cout << "[gateway] New user registered: " << request.username << std::endl;
        }
    }

    SendFrame(*ctx.transport,
        static_cast<uint16_t>(isRegister ? MessageType::RegisterResponse : MessageType::AuthResponse),
        frame.sequence, response.Serialize());
}

// Выбор комнаты: просто переключение в сессии, без записей в БД.
void HandleSelectRoom(ClientContext& ctx, const Frame& frame) {
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);

    if ((EffectivePermissionsInRoom(ctx.session, request.roomId) & static_cast<uint32_t>(Permission::OpenChannel)) == 0) {
        std::cout << "[gateway] userId=" << ctx.session->userId
            << " lacks OpenChannel in room " << request.roomId << std::endl;
        StatusResponsePayload forbidden;
        forbidden.status = 254;
        SendToSession(ctx.session, MessageType::JoinRoomResponse, forbidden.Serialize());
        return;
    }

    if (GetModerationStatus(ctx.session, request.roomId).banned) {
        std::cout << "[gateway] userId=" << ctx.session->userId
            << " is banned from room " << request.roomId << std::endl;
        StatusResponsePayload forbidden;
        forbidden.status = 253; // забанен в этом канале
        SendToSession(ctx.session, MessageType::JoinRoomResponse, forbidden.Serialize());
        return;
    }

    int64_t previous = ctx.session->currentRoomId.exchange(request.roomId);

    StatusResponsePayload response;
    response.status = 0;
    SendToSession(ctx.session, MessageType::JoinRoomResponse, response.Serialize());

    std::cout << "[gateway] userId=" << ctx.session->userId
        << " switched room " << previous << " -> " << request.roomId << std::endl;
}

void HandleLeaveRoom(ClientContext& ctx, const Frame& frame) {
    ctx.session->currentRoomId.store(0);

    StatusResponsePayload response;
    response.status = 0;
    SendToSession(ctx.session, MessageType::RoomLeaveResponse, response.Serialize());
}

void HandleRoomList(ClientContext& ctx, const Frame& frame) {
    Frame roomResponse;
    if (!CallRoom(MessageType::RoomListRequest, {}, MessageType::RoomListResponse, roomResponse)) {
        return;
    }

    // Комнаты без ViewChannel у пользователя в списке не показываем. Заодно
    // проставляем canSendMessages по тем же эффективным правам — клиент отключает
    // по нему поле ввода, вместо того чтобы узнавать о запрете только по молчаливому
    // дропу отправленного текста (см. системную комнату и оверрайды по каналам).
    auto allRooms = RoomListResponsePayload::Deserialize(roomResponse.payload);
    RoomListResponsePayload visibleRooms;
    for (auto room : allRooms.rooms) {
        uint32_t effective = EffectivePermissionsInRoom(ctx.session, room.id);
        if (effective & static_cast<uint32_t>(Permission::ViewChannel)) {
            room.canSendMessages = (effective & static_cast<uint32_t>(Permission::SendMessages)) != 0;
            visibleRooms.rooms.push_back(room);
        }
    }
    SendToSession(ctx.session, MessageType::RoomListResponse, visibleRooms.Serialize());
}

void HandleRoomCreate(ClientContext& ctx, const Frame& frame) {
    Frame roomResponse = ProxyToService(ctx, frame, Permission::ManageChannel, CallRoom,
        MessageType::RoomCreateRequest, MessageType::RoomCreateResponse, RoomCreateResponsePayload{254});
    if (roomResponse.messageType == 0) return; // отказано или сервис недоступен — ProxyToService уже ответил клиенту

    // Если создание удалось — уведомляем остальных
    auto created = RoomCreateResponsePayload::Deserialize(roomResponse.payload);
    if (created.status == 0) {
        auto request = RoomCreateRequestPayload::Deserialize(frame.payload);

        RoomCreatedPayload notification;
        notification.roomId = created.roomId;
        notification.name = request.name;
        notification.type = request.type;

        // Создателя исключаем: он уже узнал о комнате из RoomCreateResponse (и о её типе —
        // из следующего ListRoomsAsync, который клиент сам запускает по этому успеху)
        int notified = BroadcastToAll(MessageType::RoomCreated, notification.Serialize(), ctx.session->userId);
        std::cout << "[gateway] Room '" << request.name << "' (id=" << created.roomId
            << ") created, notified " << notified << " users" << std::endl;
    }
}

void HandleRoomUpdate(ClientContext& ctx, const Frame& frame) {
    Frame roomResponse = ProxyToService(ctx, frame, Permission::ManageChannel, CallRoom,
        MessageType::RoomUpdateRequest, MessageType::RoomUpdateResponse, StatusResponsePayload{254});
    if (roomResponse.messageType == 0) return;

    auto updated = StatusResponsePayload::Deserialize(roomResponse.payload);
    if (updated.status == 0) {
        auto request = RoomUpdateRequestPayload::Deserialize(frame.payload);

        RoomUpdatedPayload notification;
        notification.roomId = request.roomId;
        notification.name = request.name;

        // Инициатора не исключаем — в отличие от создания, у него нет отдельного
        // источника нового имени: RoomUpdateResponse несёт только статус.
        int notified = BroadcastToAll(MessageType::RoomUpdated, notification.Serialize());
        std::cout << "[gateway] Room " << request.roomId << " renamed to '" << request.name
            << "', notified " << notified << " users" << std::endl;
    }
}

void HandleRoomDelete(ClientContext& ctx, const Frame& frame) {
    Frame roomResponse = ProxyToService(ctx, frame, Permission::ManageChannel, CallRoom,
        MessageType::RoomDeleteRequest, MessageType::RoomDeleteResponse, StatusResponsePayload{254});
    if (roomResponse.messageType == 0) return;

    auto deleted = StatusResponsePayload::Deserialize(roomResponse.payload);
    if (deleted.status == 0) {
        auto request = RoomDeleteRequestPayload::Deserialize(frame.payload);

        // Сначала выкинуть тех, кто держал канал открытым (сбрасывает currentRoomId
        // на сервере — иначе они могли бы продолжать писать в уже несуществующую
        // комнату), и только потом — общий пуш "уберите её из списка".
        ForceLeaveRoomForAll(request.roomId);

        RoomDeletedPayload notification;
        notification.roomId = request.roomId;
        int notified = BroadcastToAll(MessageType::RoomDeleted, notification.Serialize());
        std::cout << "[gateway] Room " << request.roomId << " deleted, notified " << notified << " users" << std::endl;
    }
}

void HandleHistory(ClientContext& ctx, const Frame& frame) {
    auto request = HistoryRequestPayload::Deserialize(frame.payload);

    // Раньше истории отдавались без проверки прав вовсе — та же дыра, которую
    // JoinRoom и TextMessage уже закрывают: без OpenChannel читать содержимое нельзя.
    if ((EffectivePermissionsInRoom(ctx.session, request.roomId) & static_cast<uint32_t>(Permission::OpenChannel)) == 0) {
        std::cout << "[gateway] userId=" << ctx.session->userId
            << " lacks OpenChannel in room " << request.roomId << " (history)" << std::endl;
        HistoryResponsePayload forbidden; // пустая история — не выдаём даже намёк на содержимое
        SendToSession(ctx.session, MessageType::HistoryResponse, forbidden.Serialize());
        return;
    }

    Frame historyResponse;
    if (!CallMessage(MessageType::HistoryRequest, frame.payload, MessageType::HistoryResponse, historyResponse)) {
        return;
    }
    SendToSession(ctx.session, MessageType::HistoryResponse, historyResponse.payload);
}

void HandleTextMessage(ClientContext& ctx, const Frame& frame) {
    auto clientMessage = ClientTextMessagePayload::Deserialize(frame.payload);

    // Антифлуд — молча дропаем, как и остальные проверки ниже (мьют, права):
    // явный ответ клиенту дал бы флудеру чёткий сигнал, что его вообще заметили.
    if (!TryConsumeRateLimitToken(ctx.session->messageTokens, ctx.session->lastMessageRefill,
            g_config.gateway.rateLimit.messageBurst, g_config.gateway.rateLimit.messagesPerSecond)) {
        std::cout << "[gateway] userId=" << ctx.session->userId << " rate-limited (TextMessage)" << std::endl;
        return;
    }

    // Писать можно только в комнату, открытую сейчас
    if (ctx.session->currentRoomId.load() != clientMessage.roomId) {
        std::cout << "[gateway] userId=" << ctx.session->userId
            << " tried to post to room " << clientMessage.roomId
            << " while in " << ctx.session->currentRoomId.load() << std::endl;
        return;
    }

    if ((EffectivePermissionsInRoom(ctx.session, clientMessage.roomId) & static_cast<uint32_t>(Permission::SendMessages)) == 0) {
        std::cout << "[gateway] userId=" << ctx.session->userId
            << " lacks SendMessages in room " << clientMessage.roomId << std::endl;
        return;
    }

    if (GetModerationStatus(ctx.session, clientMessage.roomId).muted) {
        std::cout << "[gateway] userId=" << ctx.session->userId
            << " is muted in room " << clientMessage.roomId << std::endl;
        return;
    }

    // 1. Сохранить в message_service
    SendMessageRequestPayload saveRequest;
    saveRequest.roomId = clientMessage.roomId;
    saveRequest.senderId = ctx.session->userId;
    saveRequest.senderName = ctx.session->username;
    saveRequest.text = clientMessage.text;

    Frame saveResponse;
    if (!CallMessage(MessageType::SendMessageRequest, saveRequest.Serialize(), MessageType::SendMessageResponse, saveResponse)) {
        std::cout << "[gateway] message_service unavailable" << std::endl;
        return;
    }

    auto saved = SendMessageResponsePayload::Deserialize(saveResponse.payload);
    if (saved.status != 0) {
        std::cout << "[gateway] Message rejected, status=" << static_cast<int>(saved.status) << std::endl;
        return;
    }

    // 2. Собрать payload для рассылки
    BroadcastTextMessagePayload broadcast;
    broadcast.messageId = saved.messageId;
    broadcast.roomId = clientMessage.roomId;
    broadcast.senderId = ctx.session->userId;
    broadcast.senderName = ctx.session->username;
    broadcast.timestamp = saved.timestamp;
    broadcast.text = clientMessage.text;

    // 3. Разослать участникам комнаты (включая отправителя)
    int delivered = BroadcastToRoom(clientMessage.roomId, MessageType::TextMessage, broadcast.Serialize());

    std::cout << "[gateway] Message " << saved.messageId << " from '" << ctx.session->username
        << "' delivered to " << delivered << " online users" << std::endl;
}

// "Я печатаю" — те же ограничения, что и у реальной отправки (открытая комната,
// SendMessages, не в муте), иначе кикнутый/замьюченный мог бы спамить индикатором.
// Разовое уведомление без ack и без явного "перестал печатать" — клиент сам гасит
// индикатор по таймауту, если новое TypingBroadcast для этого senderId не пришло.
void HandleTyping(ClientContext& ctx, const Frame& frame) {
    auto request = TypingRequestPayload::Deserialize(frame.payload);

    // Свой, более щедрый бакет — типировать легитимно чаще, чем слать сообщения,
    // но лимит всё равно нужен: клиент троттлит "печатает" сам, а сервер этому
    // не доверяет (модифицированный клиент мог бы слать TypingRequest без ограничений).
    if (!TryConsumeRateLimitToken(ctx.session->typingTokens, ctx.session->lastTypingRefill,
            g_config.gateway.rateLimit.typingBurst, g_config.gateway.rateLimit.typingPerSecond)) {
        return;
    }

    if (ctx.session->currentRoomId.load() != request.roomId) return;
    if ((EffectivePermissionsInRoom(ctx.session, request.roomId) & static_cast<uint32_t>(Permission::SendMessages)) == 0) return;
    if (GetModerationStatus(ctx.session, request.roomId).muted) return;

    TypingBroadcastPayload broadcast;
    broadcast.roomId = request.roomId;
    broadcast.senderId = ctx.session->userId;
    broadcast.senderName = ctx.session->username;

    BroadcastToRoom(request.roomId, MessageType::TypingBroadcast, broadcast.Serialize(), ctx.session->userId);
}

// Аватарка не секрет — доступна любому залогиненному (как список ролей/участников),
// поэтому raw-forward без ProxyToService: тот же тип и клиент->gateway, и gateway->auth.
void HandleAvatarFetch(ClientContext& ctx, const Frame& frame) {
    Frame response;
    if (!CallAuth(MessageType::AvatarFetchRequest, frame.payload, MessageType::AvatarFetchResponse, response)) return;
    SendToSession(ctx.session, MessageType::AvatarFetchResponse, response.payload);
}

// Composite: клиент присылает только байты, userId подставляет сам gateway из
// сессии — иначе можно было бы выдать себя за чужую аватарку, назвав в запросе
// чужой id (в отличие от AvatarFetchRequest, где узнать чужую аватарку — не проблема).
void HandleAvatarUpload(ClientContext& ctx, const Frame& frame) {
    auto request = AvatarBytesPayload::Deserialize(frame.payload);
    AvatarUploadResponsePayload response;

    if (request.data.size() > static_cast<size_t>(g_config.gateway.avatarMaxSizeBytes)) {
        response.status = 1;  // слишком большой файл
        SendToSession(ctx.session, MessageType::AvatarUploadResponse, response.Serialize());
        return;
    }

    SetUserAvatarRequestPayload internalRequest;
    internalRequest.userId = ctx.session->userId;
    internalRequest.data = std::move(request.data);

    Frame authResponse;
    if (!CallAuth(MessageType::SetUserAvatarRequest, internalRequest.Serialize(), MessageType::SetUserAvatarResponse, authResponse)) {
        response.status = 2;  // сервис недоступен
        SendToSession(ctx.session, MessageType::AvatarUploadResponse, response.Serialize());
        return;
    }

    auto saved = SetUserAvatarResponsePayload::Deserialize(authResponse.payload);
    response.status = saved.status;
    response.version = saved.version;
    SendToSession(ctx.session, MessageType::AvatarUploadResponse, response.Serialize());

    // Версия попадёт в UserInfo у всех — каждый клиент сам решит, перекачивать ли
    // байты (сравнив с уже закэшированной версией), см. avatarVersion в RoleMessages.h.
    if (saved.status == 0) BroadcastUserListToAll();
}

// Иконка сервера — один файл на весь деплой, у gateway нет своей БД, поэтому
// обрабатывается прямо здесь, без похода в auth. Единственный из всей группы разрешён
// и ДО логина (см. допуск в главном диспетчере) — иконка сервера должна быть видна
// в рейле сразу при подключении, а не только после успешного входа; поэтому здесь
// нет SessionPtr, если клиент ещё не залогинен, и ответ шлём прямо в ctx.transport.
void HandleServerIconFetch(ClientContext& ctx, const Frame& frame) {
    ServerIconFetchResponsePayload response;
    response.version = g_serverIcon.version;
    response.data = g_serverIcon.bytes;
    auto payload = response.Serialize();

    if (ctx.session) {
        SendToSession(ctx.session, MessageType::ServerIconFetchResponse, payload);
    }
    else if (ctx.transport) {
        SendFrame(*ctx.transport, static_cast<uint16_t>(MessageType::ServerIconFetchResponse), 0, payload);
    }
}

void HandleServerIconUpload(ClientContext& ctx, const Frame& frame) {
    AvatarUploadResponsePayload response;

    if (!HasPermission(ctx.session, Permission::ManageServer)) {
        response.status = 254;
        SendToSession(ctx.session, MessageType::ServerIconUploadResponse, response.Serialize());
        return;
    }

    auto request = AvatarBytesPayload::Deserialize(frame.payload);
    if (request.data.size() > static_cast<size_t>(g_config.gateway.avatarMaxSizeBytes)) {
        response.status = 1;  // слишком большой файл
        SendToSession(ctx.session, MessageType::ServerIconUploadResponse, response.Serialize());
        return;
    }

    if (!SaveServerIcon(request.data)) {
        response.status = 2;  // не удалось записать на диск
        SendToSession(ctx.session, MessageType::ServerIconUploadResponse, response.Serialize());
        return;
    }

    response.status = 0;
    response.version = g_serverIcon.version;
    SendToSession(ctx.session, MessageType::ServerIconUploadResponse, response.Serialize());

    // Иконка общая на всех — досылаем её сразу всем, кто сейчас в сети, а не
    // заставляем ждать следующего логина/явного запроса (как с UserListResponse).
    ServerIconFetchResponsePayload broadcast;
    broadcast.version = g_serverIcon.version;
    broadcast.data = g_serverIcon.bytes;
    BroadcastToAll(MessageType::ServerIconFetchResponse, broadcast.Serialize());
}

// Разрешён и до логина (см. допуск в главном диспетчере), как ServerIconFetchRequest —
// имя сервера должно быть видно в рейле сразу при подключении.
void HandleServerInfoFetch(ClientContext& ctx, const Frame& frame) {
    ServerInfoPayload response;
    response.name = g_serverInfo.name;
    response.description = g_serverInfo.description;
    auto payload = response.Serialize();

    if (ctx.session) {
        SendToSession(ctx.session, MessageType::ServerInfoResponse, payload);
    }
    else if (ctx.transport) {
        SendFrame(*ctx.transport, static_cast<uint16_t>(MessageType::ServerInfoResponse), 0, payload);
    }
}

void HandleSetServerInfo(ClientContext& ctx, const Frame& frame) {
    StatusResponsePayload response;

    if (!HasPermission(ctx.session, Permission::ManageServer)) {
        response.status = 254;
        SendToSession(ctx.session, MessageType::SetServerInfoResponse, response.Serialize());
        return;
    }

    auto request = ServerInfoPayload::Deserialize(frame.payload);
    if (request.name.size() > kServerNameMaxLength || request.description.size() > kServerDescriptionMaxLength) {
        response.status = 1;  // слишком длинное значение
        SendToSession(ctx.session, MessageType::SetServerInfoResponse, response.Serialize());
        return;
    }

    if (!SaveServerInfo(request.name, request.description)) {
        response.status = 2;  // не удалось записать на диск
        SendToSession(ctx.session, MessageType::SetServerInfoResponse, response.Serialize());
        return;
    }

    response.status = 0;
    SendToSession(ctx.session, MessageType::SetServerInfoResponse, response.Serialize());

    ServerInfoPayload broadcast;
    broadcast.name = g_serverInfo.name;
    broadcast.description = g_serverInfo.description;
    BroadcastToAll(MessageType::ServerInfoResponse, broadcast.Serialize());
}

// Список ролей виден любому залогиненному пользователю — сама по себе не секрет,
// поэтому единственная из всей группы обходится без ProxyToService.
void HandleRoleList(ClientContext& ctx, const Frame& frame) {
    Frame roleResponse;
    if (!CallAuth(MessageType::RoleListRequest, {}, MessageType::RoleListResponse, roleResponse)) {
        return;
    }
    SendToSession(ctx.session, MessageType::RoleListResponse, roleResponse.payload);
}

// Список пользователей виден любому залогиненному — как и список ролей, сам по
// себе не секрет (нужен всем для панели участников), поэтому без ProxyToService.
void HandleUserList(ClientContext& ctx, const Frame& frame) {
    UserListResponsePayload result;
    if (!FetchUserListWithPresence(result)) return;
    SendToSession(ctx.session, MessageType::UserListResponse, result.Serialize());
}

void HandleRoleCreate(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::ManageRoles, CallAuth,
        MessageType::RoleCreateRequest, MessageType::RoleCreateResponse, RoleCreateResponsePayload{254});
}

void HandleRoleUpdate(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::ManageRoles, CallAuth,
        MessageType::RoleUpdateRequest, MessageType::RoleUpdateResponse, StatusResponsePayload{254});
}

void HandleRoleDelete(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::ManageRoles, CallAuth,
        MessageType::RoleDeleteRequest, MessageType::RoleDeleteResponse, StatusResponsePayload{254});
}

// Не через ProxyToService, как остальные Role*: нужно разобрать userId ДО форварда,
// чтобы проверить неприкосновенность владельца, и обновить кэш прав ПОСЛЕ успеха.
void HandleRoleAssign(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageRoles)) {
        SendToSession(ctx.session, MessageType::RoleAssignResponse, StatusResponsePayload{254}.Serialize());
        return;
    }
    auto request = RoleMembershipRequestPayload::Deserialize(frame.payload);
    if (request.userId != ctx.session->userId && IsTargetOwner(request.userId)) {
        SendToSession(ctx.session, MessageType::RoleAssignResponse, StatusResponsePayload{254}.Serialize());
        return;
    }

    Frame authResponse;
    if (!CallAuth(MessageType::RoleAssignRequest, frame.payload, MessageType::RoleAssignResponse, authResponse)) return;
    SendToSession(ctx.session, MessageType::RoleAssignResponse, authResponse.payload);

    if (StatusResponsePayload::Deserialize(authResponse.payload).status == 0) {
        RefreshPermissionsIfOnline(request.userId);
    }
}

void HandleRoleRemove(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageRoles)) {
        SendToSession(ctx.session, MessageType::RoleRemoveResponse, StatusResponsePayload{254}.Serialize());
        return;
    }
    auto request = RoleMembershipRequestPayload::Deserialize(frame.payload);
    if (request.userId != ctx.session->userId && IsTargetOwner(request.userId)) {
        SendToSession(ctx.session, MessageType::RoleRemoveResponse, StatusResponsePayload{254}.Serialize());
        return;
    }

    Frame authResponse;
    if (!CallAuth(MessageType::RoleRemoveRequest, frame.payload, MessageType::RoleRemoveResponse, authResponse)) return;
    SendToSession(ctx.session, MessageType::RoleRemoveResponse, authResponse.payload);

    if (StatusResponsePayload::Deserialize(authResponse.payload).status == 0) {
        RefreshPermissionsIfOnline(request.userId);
    }
}

void HandleGetChannelOverrides(ClientContext& ctx, const Frame& frame) {
    // Пустой список (не HasPermission-отказ явным статусом) — не отдаём даже намёк
    // на настройку канала тому, кому не положено её видеть.
    ProxyToService(ctx, frame, Permission::ManageChannel, CallRoom,
        MessageType::ChannelOverridesRequest, MessageType::ChannelOverridesResponse, ChannelOverridesResponsePayload{});
}

void HandleSetChannelOverride(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::ManageChannel, CallRoom,
        MessageType::SetChannelOverrideRequest, MessageType::SetChannelOverrideResponse, StatusResponsePayload{254});
}

void HandleDeleteChannelOverride(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::ManageChannel, CallRoom,
        MessageType::DeleteChannelOverrideRequest, MessageType::DeleteChannelOverrideResponse, StatusResponsePayload{254});
}

// Не через ProxyToService: нужно разобрать userId до форварда, чтобы проверить
// неприкосновенность владельца — иначе его выкинули бы из канала прежде, чем
// успели бы отказать.
void HandleChannelKick(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::KickMembers)) {
        SendToSession(ctx.session, MessageType::ChannelKickResponse, StatusResponsePayload{254}.Serialize());
        return;
    }
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    if (IsTargetOwner(request.userId)) {
        SendToSession(ctx.session, MessageType::ChannelKickResponse, StatusResponsePayload{254}.Serialize());
        return;
    }

    Frame roomResponse;
    if (!CallRoom(MessageType::ChannelKickRequest, frame.payload, MessageType::ChannelKickResponse, roomResponse)) return;
    SendToSession(ctx.session, MessageType::ChannelKickResponse, roomResponse.payload);

    if (StatusResponsePayload::Deserialize(roomResponse.payload).status == 0) {
        ForceLeaveRoomIfOnline(request.userId, request.roomId);
    }
}

void HandleChannelUnban(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::KickMembers, CallRoom,
        MessageType::ChannelUnbanRequest, MessageType::ChannelUnbanResponse, StatusResponsePayload{254});
}

void HandleChannelMute(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageChannelModeration)) {
        SendToSession(ctx.session, MessageType::ChannelMuteResponse, StatusResponsePayload{254}.Serialize());
        return;
    }
    auto request = RoomMembershipRequestPayload::Deserialize(frame.payload);
    if (IsTargetOwner(request.userId)) {
        SendToSession(ctx.session, MessageType::ChannelMuteResponse, StatusResponsePayload{254}.Serialize());
        return;
    }

    Frame roomResponse;
    if (!CallRoom(MessageType::ChannelMuteRequest, frame.payload, MessageType::ChannelMuteResponse, roomResponse)) return;
    SendToSession(ctx.session, MessageType::ChannelMuteResponse, roomResponse.payload);
}

void HandleChannelUnmute(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::ManageChannelModeration, CallRoom,
        MessageType::ChannelUnmuteRequest, MessageType::ChannelUnmuteResponse, StatusResponsePayload{254});
}

void HandleIpBanList(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::ManageServerBans, CallAuth,
        MessageType::IpBanListRequest, MessageType::IpBanListResponse, IpBanListResponsePayload{});
}

void HandleIpBan(ClientContext& ctx, const Frame& frame) {
    Frame authResponse = ProxyToService(ctx, frame, Permission::ManageServerBans, CallAuth,
        MessageType::IpBanRequest, MessageType::IpBanResponse, StatusResponsePayload{254});
    if (authResponse.messageType == 0) return;

    auto request = IpPayload::Deserialize(frame.payload);
    DisconnectSessionsForIp(request.ip);
}

void HandleIpUnban(ClientContext& ctx, const Frame& frame) {
    ProxyToService(ctx, frame, Permission::ManageServerBans, CallAuth,
        MessageType::IpUnbanRequest, MessageType::IpUnbanResponse, StatusResponsePayload{254});
}

// Действие "Заблокировать" в панели участников — банит IP текущей активной сессии
// пользователя. Composite, не через ProxyToService: клиентский payload (userId) не
// совпадает по форме с тем, что реально уходит в auth (ip), поэтому собираем сами.
void HandleBanUserSession(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageServerBans)) {
        SendToSession(ctx.session, MessageType::BanUserSessionResponse, StatusResponsePayload{254}.Serialize());
        return;
    }

    auto request = BanUserSessionRequestPayload::Deserialize(frame.payload);

    StatusResponsePayload response;
    if (request.userId == ctx.session->userId) {
        // Не даём случайно отрезать себе доступ — клиент уже прячет эту кнопку
        // для своей же строки, но что-то другое (test_client, будущий баг) может
        // прислать такой запрос напрямую, так что проверяем и здесь тоже.
        response.status = 2; // нельзя заблокировать самого себя
        SendToSession(ctx.session, MessageType::BanUserSessionResponse, response.Serialize());
        return;
    }

    if (IsTargetOwner(request.userId)) {
        response.status = 3; // нельзя заблокировать владельца сервера
        SendToSession(ctx.session, MessageType::BanUserSessionResponse, response.Serialize());
        return;
    }

    auto sessions = g_sessions.GetSessionsForUsers({ request.userId });

    if (sessions.empty()) {
        response.status = 1; // пользователь сейчас не в сети — банить нечего (нет IP)
        SendToSession(ctx.session, MessageType::BanUserSessionResponse, response.Serialize());
        return;
    }

    std::string ip = sessions.front()->remoteIp;

    IpPayload banRequest;
    banRequest.ip = ip;
    Frame authResponse;
    if (!CallAuth(MessageType::IpBanRequest, banRequest.Serialize(), MessageType::IpBanResponse, authResponse)) {
        response.status = 9; // auth_service недоступен
        SendToSession(ctx.session, MessageType::BanUserSessionResponse, response.Serialize());
        return;
    }

    DisconnectSessionsForIp(ip);

    response.status = 0;
    SendToSession(ctx.session, MessageType::BanUserSessionResponse, response.Serialize());
    std::cout << "[gateway] Banned session IP " << ip << " (userId=" << request.userId << ")" << std::endl;
}

// Действие "Удалить" в панели участников — сносит аккаунт целиком (не через
// ProxyToService: нужно разобрать userId до форварда для само-/owner-проверок,
// и оборвать активную сессию + разослать всем свежий список ПОСЛЕ успеха).
void HandleDeleteUser(ClientContext& ctx, const Frame& frame) {
    if (!HasPermission(ctx.session, Permission::ManageUsers)) {
        SendToSession(ctx.session, MessageType::DeleteUserResponse, StatusResponsePayload{254}.Serialize());
        return;
    }

    auto request = DeleteUserRequestPayload::Deserialize(frame.payload);

    StatusResponsePayload response;
    if (request.userId == ctx.session->userId) {
        response.status = 2; // нельзя удалить самого себя
        SendToSession(ctx.session, MessageType::DeleteUserResponse, response.Serialize());
        return;
    }

    if (IsTargetOwner(request.userId)) {
        response.status = 3; // владелец неприкосновенен
        SendToSession(ctx.session, MessageType::DeleteUserResponse, response.Serialize());
        return;
    }

    Frame authResponse;
    if (!CallAuth(MessageType::DeleteUserRequest, frame.payload, MessageType::DeleteUserResponse, authResponse)) return;
    SendToSession(ctx.session, MessageType::DeleteUserResponse, authResponse.payload);

    if (StatusResponsePayload::Deserialize(authResponse.payload).status == 0) {
        for (const auto& session : g_sessions.GetSessionsForUsers({ request.userId })) {
            ShutdownSocket(session->rawSocket);
        }
        BroadcastUserListToAll();
        std::cout << "[gateway] Deleted account userId=" << request.userId << std::endl;
    }
}

void ClientThread(socket_t clientSocket, std::string remoteIp) {
    ClientContext ctx;
    ctx.socket = clientSocket;
    ctx.remoteIp = remoteIp;

    if (IsIpBanned(remoteIp)) {
        std::cout << "[gateway] Rejected connection from banned IP " << remoteIp << std::endl;
        CloseSocket(clientSocket);
        return;
    }

    // Ставим таймаут на чтение до хендшейка — иначе зависший на TLS клиент
    // навсегда займёт поток, так же как раньше это грозило голому recv().
    SetRecvTimeout(clientSocket, g_config.gateway.recvTimeoutMs);

    ctx.transport = TlsTransport::AcceptServer(g_tls->Get(), clientSocket);
    if (!ctx.transport) {
        std::cout << "[gateway] TLS handshake failed, dropping connection" << std::endl;
        CloseSocket(ctx.socket);
        return;
    }
    std::cout << "[gateway] Client connected (TLS), ip=" << remoteIp << std::endl;

    auto lastActivity = std::chrono::steady_clock::now();

    while (true) {
        Frame frame;
        FrameResult result = ReceiveFrame(*ctx.transport, frame);

        if (result == FrameResult::Timeout) {
            auto idleSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - lastActivity).count();

            if (idleSeconds >= g_config.gateway.clientIdleTimeoutSec) {
                std::cout << "[gateway] Client timed out (userId="
                    << (ctx.session ? ctx.session->userId : 0)
                    << ", idle " << idleSeconds << "s)" << std::endl;
                break;
            }
            continue;   // просто тишина, ждём дальше
        }

        if (result != FrameResult::Ok) {
            std::cout << "[gateway] Client disconnected" << std::endl;
            break;
        }

        lastActivity = std::chrono::steady_clock::now();

        MessageType type = static_cast<MessageType>(frame.messageType);
        bool isAuthenticated = (ctx.session != nullptr);

        if (!isAuthenticated
            && type != MessageType::AuthRequest
            && type != MessageType::RegisterRequest
            && type != MessageType::Ping
            && type != MessageType::ServerIconFetchRequest
            && type != MessageType::ServerInfoRequest) {
            std::cout << "[gateway] Rejected 0x" << std::hex << frame.messageType
                << std::dec << " from unauthenticated client" << std::endl;
            continue;
        }

        switch (type) {
        case MessageType::RegisterRequest:   HandleAuth(ctx, frame, true);  break;
        case MessageType::AuthRequest:       HandleAuth(ctx, frame, false); break;
        case MessageType::RoomCreateRequest: HandleRoomCreate(ctx, frame);  break;
        case MessageType::RoomUpdateRequest: HandleRoomUpdate(ctx, frame);  break;
        case MessageType::RoomDeleteRequest: HandleRoomDelete(ctx, frame);  break;
        case MessageType::RoomListRequest:   HandleRoomList(ctx, frame);    break;
        case MessageType::JoinRoom:          HandleSelectRoom(ctx, frame);  break;
        case MessageType::LeaveRoom:         HandleLeaveRoom(ctx, frame);   break;
        case MessageType::HistoryRequest:    HandleHistory(ctx, frame);     break;
        case MessageType::TextMessage:       HandleTextMessage(ctx, frame); break;
        case MessageType::TypingRequest:     HandleTyping(ctx, frame);      break;
        case MessageType::RoleListRequest:   HandleRoleList(ctx, frame);    break;
        case MessageType::UserListRequest:   HandleUserList(ctx, frame);    break;
        case MessageType::RoleCreateRequest: HandleRoleCreate(ctx, frame);  break;
        case MessageType::RoleUpdateRequest: HandleRoleUpdate(ctx, frame);  break;
        case MessageType::RoleDeleteRequest: HandleRoleDelete(ctx, frame);  break;
        case MessageType::RoleAssignRequest: HandleRoleAssign(ctx, frame);  break;
        case MessageType::RoleRemoveRequest: HandleRoleRemove(ctx, frame);  break;
        case MessageType::ChannelOverridesRequest:      HandleGetChannelOverrides(ctx, frame);    break;
        case MessageType::SetChannelOverrideRequest:    HandleSetChannelOverride(ctx, frame);     break;
        case MessageType::DeleteChannelOverrideRequest: HandleDeleteChannelOverride(ctx, frame);  break;
        case MessageType::ChannelKickRequest:   HandleChannelKick(ctx, frame);   break;
        case MessageType::ChannelUnbanRequest:  HandleChannelUnban(ctx, frame);  break;
        case MessageType::ChannelMuteRequest:   HandleChannelMute(ctx, frame);   break;
        case MessageType::ChannelUnmuteRequest: HandleChannelUnmute(ctx, frame); break;
        case MessageType::IpBanListRequest: HandleIpBanList(ctx, frame); break;
        case MessageType::IpBanRequest:     HandleIpBan(ctx, frame);     break;
        case MessageType::IpUnbanRequest:   HandleIpUnban(ctx, frame);   break;
        case MessageType::BanUserSessionRequest: HandleBanUserSession(ctx, frame); break;
        case MessageType::DeleteUserRequest: HandleDeleteUser(ctx, frame); break;
        case MessageType::AvatarFetchRequest:      HandleAvatarFetch(ctx, frame);      break;
        case MessageType::AvatarUploadRequest:     HandleAvatarUpload(ctx, frame);     break;
        case MessageType::ServerIconFetchRequest:  HandleServerIconFetch(ctx, frame);  break;
        case MessageType::ServerIconUploadRequest: HandleServerIconUpload(ctx, frame); break;
        case MessageType::ServerInfoRequest:       HandleServerInfoFetch(ctx, frame);  break;
        case MessageType::SetServerInfoRequest:    HandleSetServerInfo(ctx, frame);    break;
        case MessageType::Ping:
            // Отвечаем только залогиненным — до логина сессии ещё нет
            if (ctx.session) SendToSession(ctx.session, MessageType::Pong, frame.payload);
            break;
        default:
            std::cout << "[gateway] Unhandled 0x" << std::hex << frame.messageType
                << std::dec << std::endl;
            break;
        }
    }

    if (ctx.session) {
        g_sessions.RemoveSession(ctx.session->sessionId);
        std::cout << "[gateway] Session closed, online=" << g_sessions.OnlineCount() << std::endl;
        BroadcastUserListToAll(); // presence поменялась — досылаем всем свежий список
    }
    // Отпускаем TLS-объект (шлёт close_notify) до закрытия самого сокета, а не после.
    ctx.session.reset();
    ctx.transport.reset();
    CloseSocket(ctx.socket);
}

int main() {
    try {
        g_config = LoadConfig();
    }
    catch (const std::exception& ex) {
        std::cerr << "[gateway] Config error: " << ex.what() << std::endl;
        return 1;
    }

    LoadServerIconAtStartup();
    LoadServerInfoAtStartup();

    SocketLibraryGuard socketLibrary;
    if (!socketLibrary.IsInitialized()) {
        std::cerr << "[gateway] Failed to initialize socket library" << std::endl;
        return 1;
    }

    try {
        g_tls = std::make_unique<TlsContext>(g_config.gateway.tls.certPath, g_config.gateway.tls.keyPath);
    }
    catch (const std::exception& ex) {
        std::cerr << "[gateway] TLS error: " << ex.what() << std::endl;
        return 1;
    }
    std::cout << "[gateway] TLS certificate fingerprint (SHA-256): " << g_tls->FingerprintHex() << std::endl;

    socket_t listenSocket = CreateListenSocket(g_config.gateway.port);
    if (listenSocket == kInvalidSocket) {
        std::cerr << "[gateway] Bind failed" << std::endl;
        return 1;
    }

    std::cout << "[gateway] Listening on port " << g_config.gateway.port << std::endl;

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        socket_t clientSocket = accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientSocket == kInvalidSocket) continue;

        char ipBuf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
        std::thread(ClientThread, clientSocket, std::string(ipBuf)).detach();
    }

    return 0;
}