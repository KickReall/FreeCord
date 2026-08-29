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
}

/// <summary>id системных ролей — совпадают с сидом в server/db/auth/migrations/002_roles.sql.</summary>
public static class RoleIds
{
    public const long Admin = 1;
    public const long Guest = 2;
}
