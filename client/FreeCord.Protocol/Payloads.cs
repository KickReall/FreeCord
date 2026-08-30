namespace FreeCord.Protocol;

public sealed class AuthRequest
{
    public string Username { get; set; } = "";
    public string Password { get; set; } = "";

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteString(Username);
        w.WriteString(Password);
        return w.ToArray();
    }
}

public sealed class AuthResponse
{
    public byte Status { get; init; }
    public ulong SessionId { get; init; }
    public uint UserId { get; init; }
    public bool IsSuccess => Status == 0;

    public static AuthResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new AuthResponse
        {
            Status = r.ReadByte(),
            SessionId = r.ReadUInt64(),
            UserId = r.ReadUInt32()
        };
    }
}

public sealed class RoomCreateRequest
{
    public string Name { get; set; } = "";

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteString(Name);
        return w.ToArray();
    }
}

public sealed class RoomCreateResponse
{
    public byte Status { get; init; }
    public long RoomId { get; init; }
    public bool IsSuccess => Status == 0;

    public static RoomCreateResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new RoomCreateResponse { Status = r.ReadByte(), RoomId = r.ReadInt64() };
    }
}

public sealed class RoomMembershipRequest
{
    public long RoomId { get; set; }
    public long UserId { get; set; } // gateway подставит из сессии, шлём 0

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoomId);
        w.WriteInt64(UserId);
        return w.ToArray();
    }
}

public sealed class StatusResponse
{
    public byte Status { get; init; }
    public bool IsSuccess => Status == 0;

    public static StatusResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new StatusResponse { Status = r.ReadByte() };
    }
}

public sealed record RoomInfo(long Id, string Name, RoomType Type = RoomType.Text);

public sealed class RoomListResponse
{
    public IReadOnlyList<RoomInfo> Rooms { get; init; } = Array.Empty<RoomInfo>();

    public static RoomListResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        uint count = r.ReadUInt32();
        var rooms = new List<RoomInfo>((int)count);
        for (uint i = 0; i < count; i++)
        {
            long id = r.ReadInt64();
            string name = r.ReadString();
            var type = (RoomType)r.ReadByte();
            rooms.Add(new RoomInfo(id, name, type));
        }
        return new RoomListResponse { Rooms = rooms };
    }
}

public sealed class HistoryRequest
{
    public long RoomId { get; set; }
    public uint Limit { get; set; } = 50;

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoomId);
        w.WriteUInt32(Limit);
        return w.ToArray();
    }
}

public sealed record ChatMessage(long Id, long RoomId, long SenderId, string SenderName, long Timestamp, string Text);

public sealed class HistoryResponse
{
    public IReadOnlyList<ChatMessage> Messages { get; init; } = Array.Empty<ChatMessage>();

    public static HistoryResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        uint count = r.ReadUInt32();
        var messages = new List<ChatMessage>((int)count);
        for (uint i = 0; i < count; i++)
        {
            messages.Add(new ChatMessage(
                r.ReadInt64(), r.ReadInt64(), r.ReadInt64(), r.ReadString(), r.ReadInt64(), r.ReadString()));
        }
        return new HistoryResponse { Messages = messages };
    }
}

public sealed class ClientTextMessage
{
    public long RoomId { get; set; }
    public string Text { get; set; } = "";

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoomId);
        w.WriteString(Text);
        return w.ToArray();
    }
}

public sealed class BroadcastTextMessage
{
    public long MessageId { get; init; }
    public long RoomId { get; init; }
    public long SenderId { get; init; }
    public string SenderName { get; init; } = "";
    public long Timestamp { get; init; }
    public string Text { get; init; } = "";

    public static BroadcastTextMessage Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new BroadcastTextMessage
        {
            MessageId = r.ReadInt64(),
            RoomId = r.ReadInt64(),
            SenderId = r.ReadInt64(),
            SenderName = r.ReadString(),
            Timestamp = r.ReadInt64(),
            Text = r.ReadString()
        };
    }
}

public sealed class UserPresence
{
    public long RoomId { get; init; }
    public long UserId { get; init; }
    public string Username { get; init; } = "";

    public static UserPresence Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new UserPresence
        {
            RoomId = r.ReadInt64(),
            UserId = r.ReadInt64(),
            Username = r.ReadString()
        };
    }
}

public sealed class RoomCreatedNotification
{
    public long RoomId { get; init; }
    public string Name { get; init; } = "";

    public static RoomCreatedNotification Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new RoomCreatedNotification
        {
            RoomId = r.ReadInt64(),
            Name = r.ReadString()
        };
    }
}

// DisplayName — заголовок группы в панели участников, отдельно от технического Name
// (которое используется в RoleUpdateRequest и т.п.).
public sealed record RoleInfo(long Id, string Name, bool IsSystem, uint Permissions, string DisplayName);

public sealed class RoleListResponse
{
    public IReadOnlyList<RoleInfo> Roles { get; init; } = Array.Empty<RoleInfo>();

    public static RoleListResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        uint count = r.ReadUInt32();
        var roles = new List<RoleInfo>((int)count);
        for (uint i = 0; i < count; i++)
        {
            roles.Add(new RoleInfo(r.ReadInt64(), r.ReadString(), r.ReadByte() != 0, r.ReadUInt32(), r.ReadString()));
        }
        return new RoleListResponse { Roles = roles };
    }
}

public sealed class RoleCreateRequest
{
    public string Name { get; set; } = "";
    public uint Permissions { get; set; }
    public string DisplayName { get; set; } = "";  // пусто — сервер подставит Name

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteString(Name);
        w.WriteUInt32(Permissions);
        w.WriteString(DisplayName);
        return w.ToArray();
    }
}

public sealed class RoleCreateResponse
{
    public byte Status { get; init; }  // 0 = ok, 1 = имя занято, 254 = недостаточно прав
    public long RoleId { get; init; }
    public bool IsSuccess => Status == 0;

    public static RoleCreateResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new RoleCreateResponse { Status = r.ReadByte(), RoleId = r.ReadInt64() };
    }
}

public sealed class RoleUpdateRequest
{
    public long RoleId { get; set; }
    public string Name { get; set; } = "";
    public uint Permissions { get; set; }
    public string DisplayName { get; set; } = "";  // пусто — сервер подставит Name

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoleId);
        w.WriteString(Name);
        w.WriteUInt32(Permissions);
        w.WriteString(DisplayName);
        return w.ToArray();
    }
}

public sealed class RoleDeleteRequest
{
    public long RoleId { get; set; }

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoleId);
        return w.ToArray();
    }
}

/// <summary>Общий payload для Assign и Remove — набор полей одинаковый.</summary>
public sealed class RoleMembershipRequest
{
    public long UserId { get; set; }
    public long RoleId { get; set; }

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(UserId);
        w.WriteInt64(RoleId);
        return w.ToArray();
    }
}

/// <summary>Эффективные права пользователя — приходит от gateway сразу после успешного логина.</summary>
public sealed class MyPermissions
{
    public uint Permissions { get; init; }
    public IReadOnlyList<long> RoleIds { get; init; } = Array.Empty<long>();

    public static MyPermissions Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        uint permissions = r.ReadUInt32();
        uint count = r.ReadUInt32();
        var roleIds = new List<long>((int)count);
        for (uint i = 0; i < count; i++) roleIds.Add(r.ReadInt64());
        return new MyPermissions { Permissions = permissions, RoleIds = roleIds };
    }
}

public sealed record ChannelOverrideInfo(long RoleId, uint Allow, uint Deny);

public sealed class ChannelOverridesRequest
{
    public long RoomId { get; set; }

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoomId);
        return w.ToArray();
    }
}

public sealed class ChannelOverridesResponse
{
    public IReadOnlyList<ChannelOverrideInfo> Overrides { get; init; } = Array.Empty<ChannelOverrideInfo>();

    public static ChannelOverridesResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        uint count = r.ReadUInt32();
        var overrides = new List<ChannelOverrideInfo>((int)count);
        for (uint i = 0; i < count; i++)
            overrides.Add(new ChannelOverrideInfo(r.ReadInt64(), r.ReadUInt32(), r.ReadUInt32()));
        return new ChannelOverridesResponse { Overrides = overrides };
    }
}

public sealed class SetChannelOverrideRequest
{
    public long RoomId { get; set; }
    public long RoleId { get; set; }
    public uint Allow { get; set; }
    public uint Deny { get; set; }

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoomId);
        w.WriteInt64(RoleId);
        w.WriteUInt32(Allow);
        w.WriteUInt32(Deny);
        return w.ToArray();
    }
}

/// <summary>Сброс оверрайда роли на канале обратно к базовым правам роли.</summary>
public sealed class DeleteChannelOverrideRequest
{
    public long RoomId { get; set; }
    public long RoleId { get; set; }

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoomId);
        w.WriteInt64(RoleId);
        return w.ToArray();
    }
}

/// <summary>Пришло сразу после кика — комната, из которой только что выкинули.</summary>
public sealed class ChannelKickedNotification
{
    public long RoomId { get; init; }

    public static ChannelKickedNotification Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new ChannelKickedNotification { RoomId = r.ReadInt64() };
    }
}

/// <summary>Общий payload для бана/разбана по IP — нужен только сам адрес.</summary>
public sealed class IpTargetRequest
{
    public string Ip { get; set; } = "";

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteString(Ip);
        return w.ToArray();
    }
}

public sealed class IpBanListResponse
{
    public IReadOnlyList<string> Ips { get; init; } = Array.Empty<string>();

    public static IpBanListResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        uint count = r.ReadUInt32();
        var ips = new List<string>((int)count);
        for (uint i = 0; i < count; i++) ips.Add(r.ReadString());
        return new IpBanListResponse { Ips = ips };
    }
}

/// <summary>Один пользователь для панели участников — группировка по ролям делается на клиенте.
/// Online проставляет только gateway (auth не знает о живых TCP-сессиях).</summary>
public sealed record UserInfo(long Id, string Username, IReadOnlyList<long> RoleIds, bool Online);

public sealed class UserListResponse
{
    public IReadOnlyList<UserInfo> Users { get; init; } = Array.Empty<UserInfo>();

    public static UserListResponse Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        uint count = r.ReadUInt32();
        var users = new List<UserInfo>((int)count);
        for (uint i = 0; i < count; i++)
        {
            long id = r.ReadInt64();
            string username = r.ReadString();
            uint roleCount = r.ReadUInt32();
            var roleIds = new List<long>((int)roleCount);
            for (uint j = 0; j < roleCount; j++) roleIds.Add(r.ReadInt64());
            bool online = r.ReadByte() != 0;
            users.Add(new UserInfo(id, username, roleIds, online));
        }
        return new UserListResponse { Users = users };
    }
}

/// <summary>Действие "Заблокировать" в панели участников — банит IP текущей сессии пользователя.</summary>
public sealed class BanUserSessionRequest
{
    public long UserId { get; set; }

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(UserId);
        return w.ToArray();
    }
}

/// <summary>Действие "Удалить" в панели участников — сносит аккаунт пользователя целиком.</summary>
public sealed class DeleteUserRequest
{
    public long UserId { get; set; }

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(UserId);
        return w.ToArray();
    }
}

/// <summary>Клиент -> gateway: "я печатаю" в этой комнате.</summary>
public sealed class TypingRequest
{
    public long RoomId { get; set; }

    public byte[] Serialize()
    {
        using var w = new PayloadWriter();
        w.WriteInt64(RoomId);
        return w.ToArray();
    }
}

/// <summary>Gateway -> клиент: кто-то печатает в этой комнате (кроме получателя самого).</summary>
public sealed class TypingNotification
{
    public long RoomId { get; init; }
    public long SenderId { get; init; }
    public string SenderName { get; init; } = "";

    public static TypingNotification Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new TypingNotification
        {
            RoomId = r.ReadInt64(),
            SenderId = r.ReadInt64(),
            SenderName = r.ReadString()
        };
    }
}

public sealed class UserRegisteredNotification
{
    public long UserId { get; init; }
    public string Username { get; init; } = "";

    public static UserRegisteredNotification Deserialize(byte[] data)
    {
        using var r = new PayloadReader(data);
        return new UserRegisteredNotification
        {
            UserId = r.ReadInt64(),
            Username = r.ReadString()
        };
    }
}