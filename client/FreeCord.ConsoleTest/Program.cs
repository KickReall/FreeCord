using FreeCord.Protocol;

var connection = new FreeCordConnection();

connection.AuthResponseReceived += r => Console.WriteLine(r.IsSuccess
    ? $"\n  [+] Logged in, userId={r.UserId}, sessionId={r.SessionId}"
    : $"\n  [!] Login failed (status={r.Status})");

connection.RegisterResponseReceived += r => Console.WriteLine(r.IsSuccess
    ? $"\n  [+] Registered, userId={r.UserId}"
    : "\n  [!] Registration failed (username taken?)");

connection.RoomCreateResponseReceived += r => Console.WriteLine(r.IsSuccess
    ? $"\n  [+] Room created, roomId={r.RoomId}"
    : "\n  [!] Room name taken");

connection.RoomListReceived += r =>
{
    Console.WriteLine("\n  --- rooms ---");
    if (r.Rooms.Count == 0) Console.WriteLine("  (none)");
    foreach (var room in r.Rooms) Console.WriteLine($"  [{room.Id}] {room.Name}");
};

connection.JoinResponseReceived += r => Console.WriteLine($"\n  Join: {Describe(r.Status)}");
connection.LeaveResponseReceived += r => Console.WriteLine($"\n  Leave: {Describe(r.Status)}");

connection.HistoryReceived += r =>
{
    Console.WriteLine("\n  --- history ---");
    if (r.Messages.Count == 0) Console.WriteLine("  (empty)");
    foreach (var m in r.Messages) Console.WriteLine($"  [{m.Id}] user{m.SenderId}: {m.Text}");
};

connection.MessageReceived += m =>
    Console.WriteLine($"\n  <room {m.RoomId}> {m.SenderName}: {m.Text}");

connection.UserJoined += p => Console.WriteLine($"\n  <room {p.RoomId}> {p.Username} joined");
connection.UserLeft += p => Console.WriteLine($"\n  <room {p.RoomId}> {p.Username} left");
connection.PongReceived += () => Console.WriteLine("\n  [+] Pong");
connection.Disconnected += ex => Console.WriteLine($"\n  [!] Disconnected: {ex?.Message ?? "closed"}");

static string Describe(byte status) => status switch
{
    0 => "OK",
    1 => "room not found",
    2 => "membership conflict",
    _ => $"error {status}"
};

try
{
    await connection.ConnectAsync("127.0.0.1", 6000);
    Console.WriteLine("[client] Connected to gateway");
}
catch (Exception ex)
{
    Console.WriteLine($"[client] Cannot connect: {ex.Message}");
    return;
}

while (true)
{
    Console.WriteLine("\n=== FreeCord (C#) ===");
    Console.WriteLine("1-Register  2-Login  3-Create room  4-List rooms");
    Console.WriteLine("5-Join  6-Leave  7-Send message  8-History  9-Ping  0-Exit");
    Console.Write("> ");

    var input = Console.ReadLine();
    if (input == "0" || input is null) break;

    switch (input)
    {
        case "1":
        case "2":
            Console.Write("  Username: "); var u = Console.ReadLine() ?? "";
            Console.Write("  Password: "); var p = Console.ReadLine() ?? "";
            if (input == "1") await connection.RegisterAsync(u, p);
            else await connection.LoginAsync(u, p);
            break;

        case "3":
            Console.Write("  Room name: ");
            await connection.CreateRoomAsync(Console.ReadLine() ?? "");
            break;

        case "4":
            await connection.ListRoomsAsync();
            break;

        case "5":
        case "6":
            Console.Write("  Room id: ");
            if (long.TryParse(Console.ReadLine(), out var roomId))
            {
                if (input == "5") await connection.JoinRoomAsync(roomId);
                else await connection.LeaveRoomAsync(roomId);
            }
            break;

        case "7":
            Console.Write("  Room id: ");
            if (long.TryParse(Console.ReadLine(), out var msgRoomId))
            {
                Console.Write("  Text: ");
                await connection.SendTextAsync(msgRoomId, Console.ReadLine() ?? "");
            }
            break;

        case "8":
            Console.Write("  Room id: ");
            if (long.TryParse(Console.ReadLine(), out var histRoomId))
                await connection.RequestHistoryAsync(histRoomId);
            break;

        case "9":
            await connection.PingAsync();
            break;
    }

    await Task.Delay(200); // даём ответу успеть напечататься до перерисовки меню
}

await connection.DisposeAsync();
Console.WriteLine("[client] Bye");