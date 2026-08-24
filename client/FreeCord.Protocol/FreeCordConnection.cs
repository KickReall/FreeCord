using System.Net.Sockets;

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
    private NetworkStream? _stream;
    private CancellationTokenSource? _cts;
    private Task? _receiveTask;

    // Отправлять могут разные потоки — сериализуем запись, иначе кадры перемешаются
    private readonly SemaphoreSlim _sendLock = new(1, 1);

    public bool IsConnected => _tcp?.Connected ?? false;

    public event Action<AuthResponse>? AuthResponseReceived;
    public event Action<AuthResponse>? RegisterResponseReceived;
    public event Action<RoomCreateResponse>? RoomCreateResponseReceived;
    public event Action<RoomListResponse>? RoomListReceived;
    public event Action<StatusResponse>? JoinResponseReceived;
    public event Action<StatusResponse>? LeaveResponseReceived;
    public event Action<HistoryResponse>? HistoryReceived;
    public event Action<BroadcastTextMessage>? MessageReceived;
    public event Action<UserPresence>? UserJoined;
    public event Action<UserPresence>? UserLeft;
    public event Action? PongReceived;
    public event Action<Exception?>? Disconnected;

    public async Task ConnectAsync(string host, int port, CancellationToken ct = default)
    {
        _tcp = new TcpClient();
        await _tcp.ConnectAsync(host, port, ct);
        _stream = _tcp.GetStream();

        _cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        _receiveTask = Task.Run(() => ReceiveLoopAsync(_cts.Token));
    }

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
            case MessageType.UserJoined:
                UserJoined?.Invoke(UserPresence.Deserialize(frame.Payload));
                break;
            case MessageType.UserLeft:
                UserLeft?.Invoke(UserPresence.Deserialize(frame.Payload));
                break;
            case MessageType.Pong:
                PongReceived?.Invoke();
                break;
        }
    }

    // --- Удобные обёртки над командами ---

    public Task LoginAsync(string username, string password) =>
        SendAsync(MessageType.AuthRequest, new AuthRequest { Username = username, Password = password }.Serialize());

    public Task RegisterAsync(string username, string password) =>
        SendAsync(MessageType.RegisterRequest, new AuthRequest { Username = username, Password = password }.Serialize());

    public Task CreateRoomAsync(string name) =>
        SendAsync(MessageType.RoomCreateRequest, new RoomCreateRequest { Name = name }.Serialize());

    public Task ListRoomsAsync() => SendAsync(MessageType.RoomListRequest);

    public Task JoinRoomAsync(long roomId) =>
        SendAsync(MessageType.JoinRoom, new RoomMembershipRequest { RoomId = roomId }.Serialize());

    public Task LeaveRoomAsync(long roomId) =>
        SendAsync(MessageType.LeaveRoom, new RoomMembershipRequest { RoomId = roomId }.Serialize());

    public Task SendTextAsync(long roomId, string text) =>
        SendAsync(MessageType.TextMessage, new ClientTextMessage { RoomId = roomId, Text = text }.Serialize());

    public Task RequestHistoryAsync(long roomId, uint limit = 50) =>
        SendAsync(MessageType.HistoryRequest, new HistoryRequest { RoomId = roomId, Limit = limit }.Serialize());

    public Task PingAsync() => SendAsync(MessageType.Ping);

    public async ValueTask DisposeAsync()
    {
        _cts?.Cancel();
        _tcp?.Close();
        if (_receiveTask is not null)
        {
            try { await _receiveTask; } catch { /* уже закрыто */ }
        }
        _cts?.Dispose();
        _tcp?.Dispose();
        _sendLock.Dispose();
    }
}