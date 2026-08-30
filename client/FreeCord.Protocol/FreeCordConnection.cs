using System.Linq;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

namespace FreeCord.Protocol;

/// <summary>Один принятый кадр: заголовок + payload</summary>
public readonly record struct Frame(MessageType Type, uint Sequence, byte[] Payload);

/// <summary>
/// Долгоживущее соединение с gateway. Приём идёт в фоновой задаче,
/// входящие сообщения приходят через события.
/// </summary>
public sealed class FreeCordConnection : IAsyncDisposable
{
    private const int HeaderSize = 10;              // uint32 length + uint16 type + uint32 sequence
    private const uint MaxPayloadSize = 16 * 1024 * 1024;

    private TcpClient? _tcp;
    private Stream? _stream;   // после подключения — SslStream; TLS обязателен, plain-соединения не бывает
    private CancellationTokenSource? _cts;
    private Task? _receiveTask;
    private Task? _pingTask;
    private readonly TrustedServerStore _trustedServers = new();
    private static readonly TimeSpan PingInterval = TimeSpan.FromSeconds(15);

    // Отправлять могут разные потоки — сериализуем запись, иначе кадры перемешаются
    private readonly SemaphoreSlim _sendLock = new(1, 1);

    public bool IsConnected => _tcp?.Connected ?? false;

    /// <summary>
    /// Отпечаток, закреплённый (TOFU) для этого host:port на прошлых подключениях —
    /// в отличие от <see cref="ServerFingerprint"/> не требует, чтобы соединение было
    /// установлено прямо сейчас: читается из уже сохранённого на диске состояния.
    /// </summary>
    public string? GetPinnedFingerprint(string host, int port) => _trustedServers.GetPinnedFingerprint(host, port);

    /// <summary>
    /// Закрепляет отпечаток заранее, до первого подключения — используется при вводе
    /// инвайт-ссылки с отпечатком: тогда самое первое подключение уже проверяется
    /// по известному значению, а не слепо доверяет тому, что пришло от сервера.
    /// Если для этого host:port уже что-то закреплено, не перезаписывает — TOFU
    /// закрепляет один раз, а не переписывается по желанию звонящего.
    /// </summary>
    public void PinFingerprintIfUnknown(string host, int port, string fingerprint)
    {
        if (_trustedServers.GetPinnedFingerprint(host, port) is null)
            _trustedServers.Pin(host, port, fingerprint);
    }

    /// <summary>
    /// Отпечаток сертификата текущего соединения — не только что закреплённого, а вообще
    /// любого успешного подключения. В отличие от host:port однозначно определяет сервер:
    /// один и тот же gateway может быть доступен под разными именами/адресами (например,
    /// 127.0.0.1 и 127.0.0.2 на loopback), а сертификат у него один.
    /// </summary>
    public string? ServerFingerprint { get; private set; }

    /// <summary>Отпечаток сертификата сервера, закреплённый при первом подключении к этому адресу (TOFU).</summary>
    public event Action<string>? ServerCertificatePinned;

    public event Action<AuthResponse>? AuthResponseReceived;
    public event Action<AuthResponse>? RegisterResponseReceived;
    public event Action<RoomCreatedNotification>? RoomCreated;
    public event Action<UserRegisteredNotification>? UserRegistered;
    public event Action<RoomCreateResponse>? RoomCreateResponseReceived;
    public event Action<RoomUpdatedNotification>? RoomUpdated;
    public event Action<StatusResponse>? RoomUpdateResponseReceived;
    public event Action<RoomDeletedNotification>? RoomDeleted;
    public event Action<StatusResponse>? RoomDeleteResponseReceived;
    public event Action<RoomListResponse>? RoomListReceived;
    public event Action<StatusResponse>? JoinResponseReceived;
    public event Action<StatusResponse>? LeaveResponseReceived;
    public event Action<HistoryResponse>? HistoryReceived;
    public event Action<RoleListResponse>? RoleListReceived;
    public event Action<RoleCreateResponse>? RoleCreateResponseReceived;
    public event Action<StatusResponse>? RoleUpdateResponseReceived;
    public event Action<StatusResponse>? RoleDeleteResponseReceived;
    public event Action<StatusResponse>? RoleAssignResponseReceived;
    public event Action<StatusResponse>? RoleRemoveResponseReceived;
    public event Action<MyPermissions>? MyPermissionsReceived;
    public event Action<ChannelOverridesResponse>? ChannelOverridesReceived;
    public event Action<StatusResponse>? SetChannelOverrideResponseReceived;
    public event Action<StatusResponse>? DeleteChannelOverrideResponseReceived;
    public event Action<StatusResponse>? ChannelKickResponseReceived;
    public event Action<StatusResponse>? ChannelUnbanResponseReceived;
    public event Action<StatusResponse>? ChannelMuteResponseReceived;
    public event Action<StatusResponse>? ChannelUnmuteResponseReceived;
    public event Action<ChannelKickedNotification>? ChannelKicked;
    public event Action<IpBanListResponse>? IpBanListReceived;
    public event Action<StatusResponse>? IpBanResponseReceived;
    public event Action<StatusResponse>? IpUnbanResponseReceived;
    public event Action<UserListResponse>? UserListReceived;
    public event Action<StatusResponse>? BanUserSessionResponseReceived;
    public event Action<StatusResponse>? DeleteUserResponseReceived;
    public event Action<AvatarUploadResponse>? AvatarUploadResponseReceived;
    public event Action<AvatarFetchResponse>? AvatarFetchResponseReceived;
    public event Action<AvatarUploadResponse>? ServerIconUploadResponseReceived;
    public event Action<ServerIconResponse>? ServerIconFetchResponseReceived;
    public event Action<ServerInfoResponse>? ServerInfoResponseReceived;
    public event Action<StatusResponse>? SetServerInfoResponseReceived;
    public event Action<BroadcastTextMessage>? MessageReceived;
    public event Action<TypingNotification>? TypingReceived;
    public event Action<UserPresence>? UserJoined;
    public event Action<UserPresence>? UserLeft;
    public event Action? PongReceived;
    public event Action<Exception?>? Disconnected;

    public async Task ConnectAsync(string host, int port, CancellationToken ct = default)
    {
        _tcp = new TcpClient();
        await _tcp.ConnectAsync(host, port, ct);

        // Сертификат самоподписанный (нет доверенного CA), поэтому обычная проверка цепочки
        // не подходит — вместо неё TOFU: сверяем отпечаток с тем, что закрепили при первом
        // подключении к этому host:port, и сами решаем, доверять ли текущему сертификату.
        string? pinnedFingerprint = _trustedServers.GetPinnedFingerprint(host, port);
        string? observedFingerprint = null;

        var sslStream = new SslStream(_tcp.GetStream(), leaveInnerStreamOpen: false,
            (_, certificate, _, _) =>
            {
                if (certificate is null) return false;
                using var cert2 = new X509Certificate2(certificate);
                observedFingerprint = FormatFingerprint(cert2.GetCertHash(HashAlgorithmName.SHA256));

                // Ничего не закреплено — первое подключение, принимаем и закрепим ниже.
                if (pinnedFingerprint is null) return true;
                return string.Equals(pinnedFingerprint, observedFingerprint, StringComparison.OrdinalIgnoreCase);
            });

        try
        {
            await sslStream.AuthenticateAsClientAsync(new SslClientAuthenticationOptions
            {
                TargetHost = host,
                EnabledSslProtocols = SslProtocols.Tls12 | SslProtocols.Tls13,
            }, ct);
        }
        catch (AuthenticationException)
        {
            sslStream.Dispose();
            if (pinnedFingerprint is not null && observedFingerprint is not null &&
                !string.Equals(pinnedFingerprint, observedFingerprint, StringComparison.OrdinalIgnoreCase))
            {
                throw new ServerCertificateChangedException(host, port, pinnedFingerprint, observedFingerprint);
            }
            throw;
        }

        if (pinnedFingerprint is null && observedFingerprint is not null)
        {
            _trustedServers.Pin(host, port, observedFingerprint);
            ServerCertificatePinned?.Invoke(observedFingerprint);
        }

        _stream = sslStream;
        ServerFingerprint = observedFingerprint;

        _cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        _receiveTask = Task.Run(() => ReceiveLoopAsync(_cts.Token));
        _pingTask = Task.Run(() => PingLoopAsync(_cts.Token));
    }

    private static string FormatFingerprint(byte[] hash) =>
        string.Join(":", hash.Select(b => b.ToString("X2")));

    public async Task SendAsync(MessageType type, byte[] payload, uint sequence = 0)
    {
        if (_stream is null) throw new InvalidOperationException("Not connected");

        var frame = new byte[HeaderSize + payload.Length];
        BitConverter.TryWriteBytes(frame.AsSpan(0, 4), (uint)payload.Length);
        BitConverter.TryWriteBytes(frame.AsSpan(4, 2), (ushort)type);
        BitConverter.TryWriteBytes(frame.AsSpan(6, 4), sequence);
        payload.CopyTo(frame, HeaderSize);

        await _sendLock.WaitAsync();
        try
        {
            await _stream.WriteAsync(frame);
            await _stream.FlushAsync();
        }
        finally
        {
            _sendLock.Release();
        }
    }

    public Task SendAsync(MessageType type) => SendAsync(type, Array.Empty<byte>());

    // Периодический ping — сообщает серверу, что клиент жив
    private async Task PingLoopAsync(CancellationToken ct)
    {
        try
        {
            while (!ct.IsCancellationRequested)
            {
                await Task.Delay(PingInterval, ct);
                await SendAsync(MessageType.Ping);
            }
        }
        catch (OperationCanceledException) { /* штатное закрытие */ }
        catch { /* соединение уже разорвано, ReceiveLoop сообщит об этом */ }
    }

    private async Task ReceiveLoopAsync(CancellationToken ct)
    {
        Exception? error = null;
        try
        {
            var header = new byte[HeaderSize];
            while (!ct.IsCancellationRequested)
            {
                // ReadExactlyAsync сам крутит цикл, пока не наберёт нужное число байт —
                // ровно то, что в C++ делает RecvAll вручную
                await _stream!.ReadExactlyAsync(header, ct);

                uint length = BitConverter.ToUInt32(header, 0);
                var type = (MessageType)BitConverter.ToUInt16(header, 4);
                uint sequence = BitConverter.ToUInt32(header, 6);

                if (length > MaxPayloadSize)
                    throw new InvalidDataException($"Payload too large: {length}");

                var payload = new byte[length];
                if (length > 0)
                    await _stream.ReadExactlyAsync(payload, ct);

                Dispatch(new Frame(type, sequence, payload));
            }
        }
        catch (OperationCanceledException) { /* штатное закрытие */ }
        catch (Exception ex) { error = ex; }

        Disconnected?.Invoke(error);
    }

    private void Dispatch(Frame frame)
    {
        switch (frame.Type)
        {
            case MessageType.AuthResponse:
                AuthResponseReceived?.Invoke(AuthResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RegisterResponse:
                RegisterResponseReceived?.Invoke(AuthResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoomCreated:
                RoomCreated?.Invoke(RoomCreatedNotification.Deserialize(frame.Payload));
                break;
            case MessageType.RoomUpdated:
                RoomUpdated?.Invoke(RoomUpdatedNotification.Deserialize(frame.Payload));
                break;
            case MessageType.RoomUpdateResponse:
                RoomUpdateResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoomDeleted:
                RoomDeleted?.Invoke(RoomDeletedNotification.Deserialize(frame.Payload));
                break;
            case MessageType.RoomDeleteResponse:
                RoomDeleteResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoomCreateResponse:
                RoomCreateResponseReceived?.Invoke(RoomCreateResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoomListResponse:
                RoomListReceived?.Invoke(RoomListResponse.Deserialize(frame.Payload));
                break;
            case MessageType.JoinRoomResponse:
                JoinResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoomLeaveResponse:
                LeaveResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.HistoryResponse:
                HistoryReceived?.Invoke(HistoryResponse.Deserialize(frame.Payload));
                break;
            case MessageType.TextMessage:
                MessageReceived?.Invoke(BroadcastTextMessage.Deserialize(frame.Payload));
                break;
            case MessageType.UserRegistered:
                UserRegistered?.Invoke(UserRegisteredNotification.Deserialize(frame.Payload));
                break;
            case MessageType.UserJoined:
                UserJoined?.Invoke(UserPresence.Deserialize(frame.Payload));
                break;
            case MessageType.UserLeft:
                UserLeft?.Invoke(UserPresence.Deserialize(frame.Payload));
                break;
            case MessageType.Pong:
                PongReceived?.Invoke();
                break;
            case MessageType.RoleListResponse:
                RoleListReceived?.Invoke(RoleListResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoleCreateResponse:
                RoleCreateResponseReceived?.Invoke(RoleCreateResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoleUpdateResponse:
                RoleUpdateResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoleDeleteResponse:
                RoleDeleteResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoleAssignResponse:
                RoleAssignResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.RoleRemoveResponse:
                RoleRemoveResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.MyPermissions:
                MyPermissionsReceived?.Invoke(MyPermissions.Deserialize(frame.Payload));
                break;
            case MessageType.ChannelOverridesResponse:
                ChannelOverridesReceived?.Invoke(ChannelOverridesResponse.Deserialize(frame.Payload));
                break;
            case MessageType.SetChannelOverrideResponse:
                SetChannelOverrideResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.DeleteChannelOverrideResponse:
                DeleteChannelOverrideResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.ChannelKickResponse:
                ChannelKickResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.ChannelUnbanResponse:
                ChannelUnbanResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.ChannelMuteResponse:
                ChannelMuteResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.ChannelUnmuteResponse:
                ChannelUnmuteResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.ChannelKicked:
                ChannelKicked?.Invoke(ChannelKickedNotification.Deserialize(frame.Payload));
                break;
            case MessageType.IpBanListResponse:
                IpBanListReceived?.Invoke(IpBanListResponse.Deserialize(frame.Payload));
                break;
            case MessageType.IpBanResponse:
                IpBanResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.IpUnbanResponse:
                IpUnbanResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.UserListResponse:
                UserListReceived?.Invoke(UserListResponse.Deserialize(frame.Payload));
                break;
            case MessageType.BanUserSessionResponse:
                BanUserSessionResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.DeleteUserResponse:
                DeleteUserResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
            case MessageType.TypingBroadcast:
                TypingReceived?.Invoke(TypingNotification.Deserialize(frame.Payload));
                break;
            case MessageType.AvatarUploadResponse:
                AvatarUploadResponseReceived?.Invoke(AvatarUploadResponse.Deserialize(frame.Payload));
                break;
            case MessageType.AvatarFetchResponse:
                AvatarFetchResponseReceived?.Invoke(AvatarFetchResponse.Deserialize(frame.Payload));
                break;
            case MessageType.ServerIconUploadResponse:
                ServerIconUploadResponseReceived?.Invoke(AvatarUploadResponse.Deserialize(frame.Payload));
                break;
            case MessageType.ServerIconFetchResponse:
                ServerIconFetchResponseReceived?.Invoke(ServerIconResponse.Deserialize(frame.Payload));
                break;
            case MessageType.ServerInfoResponse:
                ServerInfoResponseReceived?.Invoke(ServerInfoResponse.Deserialize(frame.Payload));
                break;
            case MessageType.SetServerInfoResponse:
                SetServerInfoResponseReceived?.Invoke(StatusResponse.Deserialize(frame.Payload));
                break;
        }
    }

    // --- Удобные обёртки над командами ---

    public Task LoginAsync(string username, string password) =>
        SendAsync(MessageType.AuthRequest, new AuthRequest { Username = username, Password = password }.Serialize());

    public Task RegisterAsync(string username, string password) =>
        SendAsync(MessageType.RegisterRequest, new AuthRequest { Username = username, Password = password }.Serialize());

    public Task CreateRoomAsync(string name, RoomType type = RoomType.Text) =>
        SendAsync(MessageType.RoomCreateRequest, new RoomCreateRequest { Name = name, Type = type }.Serialize());

    public Task UpdateRoomAsync(long roomId, string name) =>
        SendAsync(MessageType.RoomUpdateRequest, new RoomUpdateRequest { RoomId = roomId, Name = name }.Serialize());

    public Task DeleteRoomAsync(long roomId) =>
        SendAsync(MessageType.RoomDeleteRequest, new RoomDeleteRequest { RoomId = roomId }.Serialize());

    public Task ListRoomsAsync() => SendAsync(MessageType.RoomListRequest);

    public Task JoinRoomAsync(long roomId) =>
        SendAsync(MessageType.JoinRoom, new RoomMembershipRequest { RoomId = roomId }.Serialize());

    public Task LeaveRoomAsync(long roomId) =>
        SendAsync(MessageType.LeaveRoom, new RoomMembershipRequest { RoomId = roomId }.Serialize());

    public Task SendTextAsync(long roomId, string text) =>
        SendAsync(MessageType.TextMessage, new ClientTextMessage { RoomId = roomId, Text = text }.Serialize());

    public Task TypingAsync(long roomId) =>
        SendAsync(MessageType.TypingRequest, new TypingRequest { RoomId = roomId }.Serialize());

    public Task RequestHistoryAsync(long roomId, uint limit = 50) =>
        SendAsync(MessageType.HistoryRequest, new HistoryRequest { RoomId = roomId, Limit = limit }.Serialize());

    public Task PingAsync() => SendAsync(MessageType.Ping);

    public Task ListRolesAsync() => SendAsync(MessageType.RoleListRequest);

    public Task CreateRoleAsync(string name, uint permissions, string displayName = "") =>
        SendAsync(MessageType.RoleCreateRequest, new RoleCreateRequest { Name = name, Permissions = permissions, DisplayName = displayName }.Serialize());

    public Task UpdateRoleAsync(long roleId, string name, uint permissions, string displayName = "") =>
        SendAsync(MessageType.RoleUpdateRequest, new RoleUpdateRequest { RoleId = roleId, Name = name, Permissions = permissions, DisplayName = displayName }.Serialize());

    public Task DeleteRoleAsync(long roleId) =>
        SendAsync(MessageType.RoleDeleteRequest, new RoleDeleteRequest { RoleId = roleId }.Serialize());

    public Task AssignRoleAsync(long userId, long roleId) =>
        SendAsync(MessageType.RoleAssignRequest, new RoleMembershipRequest { UserId = userId, RoleId = roleId }.Serialize());

    public Task RemoveRoleAsync(long userId, long roleId) =>
        SendAsync(MessageType.RoleRemoveRequest, new RoleMembershipRequest { UserId = userId, RoleId = roleId }.Serialize());

    public Task GetChannelOverridesAsync(long roomId) =>
        SendAsync(MessageType.ChannelOverridesRequest, new ChannelOverridesRequest { RoomId = roomId }.Serialize());

    public Task SetChannelOverrideAsync(long roomId, long roleId, uint allow, uint deny) =>
        SendAsync(MessageType.SetChannelOverrideRequest,
            new SetChannelOverrideRequest { RoomId = roomId, RoleId = roleId, Allow = allow, Deny = deny }.Serialize());

    public Task DeleteChannelOverrideAsync(long roomId, long roleId) =>
        SendAsync(MessageType.DeleteChannelOverrideRequest, new DeleteChannelOverrideRequest { RoomId = roomId, RoleId = roleId }.Serialize());

    // Кик = бан от канала + принудительный выход, если пользователь сейчас онлайн в нём
    public Task KickFromChannelAsync(long roomId, long userId) =>
        SendAsync(MessageType.ChannelKickRequest, new RoomMembershipRequest { RoomId = roomId, UserId = userId }.Serialize());

    public Task UnbanFromChannelAsync(long roomId, long userId) =>
        SendAsync(MessageType.ChannelUnbanRequest, new RoomMembershipRequest { RoomId = roomId, UserId = userId }.Serialize());

    public Task MuteInChannelAsync(long roomId, long userId) =>
        SendAsync(MessageType.ChannelMuteRequest, new RoomMembershipRequest { RoomId = roomId, UserId = userId }.Serialize());

    public Task UnmuteInChannelAsync(long roomId, long userId) =>
        SendAsync(MessageType.ChannelUnmuteRequest, new RoomMembershipRequest { RoomId = roomId, UserId = userId }.Serialize());

    public Task ListBannedIpsAsync() => SendAsync(MessageType.IpBanListRequest);

    public Task BanIpAsync(string ip) =>
        SendAsync(MessageType.IpBanRequest, new IpTargetRequest { Ip = ip }.Serialize());

    public Task UnbanIpAsync(string ip) =>
        SendAsync(MessageType.IpUnbanRequest, new IpTargetRequest { Ip = ip }.Serialize());

    public Task ListUsersAsync() => SendAsync(MessageType.UserListRequest);

    public Task BanUserSessionAsync(long userId) =>
        SendAsync(MessageType.BanUserSessionRequest, new BanUserSessionRequest { UserId = userId }.Serialize());

    public Task DeleteUserAsync(long userId) =>
        SendAsync(MessageType.DeleteUserRequest, new DeleteUserRequest { UserId = userId }.Serialize());

    public Task UploadAvatarAsync(byte[] data) =>
        SendAsync(MessageType.AvatarUploadRequest, new AvatarBytesRequest { Data = data }.Serialize());

    public Task FetchAvatarAsync(long userId) =>
        SendAsync(MessageType.AvatarFetchRequest, new AvatarFetchRequest { UserId = userId }.Serialize());

    public Task UploadServerIconAsync(byte[] data) =>
        SendAsync(MessageType.ServerIconUploadRequest, new AvatarBytesRequest { Data = data }.Serialize());

    public Task FetchServerIconAsync() => SendAsync(MessageType.ServerIconFetchRequest);

    public Task FetchServerInfoAsync() => SendAsync(MessageType.ServerInfoRequest);

    public Task SetServerInfoAsync(string name, string description) =>
        SendAsync(MessageType.SetServerInfoRequest, new SetServerInfoRequest { Name = name, Description = description }.Serialize());

    public async ValueTask DisposeAsync()
    {
        _cts?.Cancel();
        _tcp?.Close();
        if (_receiveTask is not null)
        {
            try { await _receiveTask; } catch { /* уже закрыто */ }
        }
        if (_pingTask is not null)
        {
            try { await _pingTask; } catch { }
        }
        _cts?.Dispose();
        _stream?.Dispose();
        _tcp?.Dispose();
        _sendLock.Dispose();
    }
}