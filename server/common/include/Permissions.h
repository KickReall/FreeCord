#pragma once
#include <cstdint>

// Битовые флаги прав ролей. Должны совпадать с client/FreeCord.Protocol/Permissions.cs —
// как и MessageType, значения нельзя менять/переставлять без зеркальной правки на клиенте.
enum class Permission : uint32_t {
    ViewChannel             = 1u << 0,  // видеть канал в списке
    OpenChannel             = 1u << 1,  // открыть/подключиться к каналу
    SendMessages            = 1u << 2,
    SendFiles               = 1u << 3,
    ManageChannel           = 1u << 4,  // создавать/переименовывать/удалять каналы, настраивать оверрайды
    ManageRoles             = 1u << 5,
    ManageServer            = 1u << 6,  // настройки сервера: имя, иконка, лимиты
    KickMembers             = 1u << 7,
    ManageChannelModeration = 1u << 8,  // кик/мьют в рамках канала
    ManageServerBans        = 1u << 9,  // бан по IP на уровне всего сервера
};

constexpr uint32_t kGuestDefaultPermissions =
    static_cast<uint32_t>(Permission::ViewChannel) |
    static_cast<uint32_t>(Permission::OpenChannel) |
    static_cast<uint32_t>(Permission::SendMessages);

// id системных ролей — совпадают с сидом в server/db/auth/migrations/002_roles.sql.
// У admin (id=1) права не хранятся битовой маской: код всегда считает его
// суперпользователем независимо от значения permissions в БД — иначе пришлось бы
// держать маску admin'а в синхроне при каждом новом добавляемом праве.
constexpr int64_t kAdminRoleId = 1;
constexpr int64_t kGuestRoleId = 2;
