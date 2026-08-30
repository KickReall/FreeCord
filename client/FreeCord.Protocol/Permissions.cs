namespace FreeCord.Protocol;

/// <summary>
/// Битовые флаги прав ролей. Должны совпадать с server/common/include/Permissions.h —
/// как и MessageType, значения нельзя менять/переставлять без зеркальной правки на сервере.
/// </summary>
[Flags]
public enum Permission : uint
{
    ViewChannel             = 1u << 0,
    OpenChannel             = 1u << 1,
    SendMessages            = 1u << 2,
    SendFiles               = 1u << 3,
    ManageChannel           = 1u << 4,
    ManageRoles             = 1u << 5,
    ManageServer            = 1u << 6,
    KickMembers             = 1u << 7,
    ManageChannelModeration = 1u << 8,
    ManageServerBans        = 1u << 9,
    ManageUsers             = 1u << 10,
    ManageMessages          = 1u << 11,
    ManageFiles             = 1u << 12,
}

/// <summary>id системных ролей — совпадают с сидом в server/db/auth/migrations/002_roles.sql
/// и server/db/auth/migrations/005_owner_role.sql.</summary>
public static class RoleIds
{
    public const long Admin = 1;
    public const long Guest = 2;

    /// <summary>Первый когда-либо зарегистрированный пользователь — неприкосновенен,
    /// сервер отказывает в кике/муте/бане/смене ролей для него от кого угодно, кроме
    /// него самого. Не даёт новых прав (permissions=0), только метка.</summary>
    public const long Owner = 3;
}

/// <summary>id системных комнат — совпадает с сидом в server/db/room/migrations/001_initial.sql
/// (SYSTEM_ROOM_ID в server/services/gateway/main.cpp).</summary>
public static class RoomIds
{
    /// <summary>Комната "system" — видна и открыта всем, переименовать/удалить нельзя
    /// (сервер отказывает), писать в неё может только admin/owner (жёсткий запрет
    /// в EffectivePermissionsInRoom, не настройка через оверрайды).</summary>
    public const long System = 1;
}
