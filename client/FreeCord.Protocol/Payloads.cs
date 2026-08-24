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

public sealed record RoomInfo(long Id, string Name);

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
            rooms.Add(new RoomInfo(id, name));
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