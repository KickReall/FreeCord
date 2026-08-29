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

connection.MyPermissionsReceived += p => Console.WriteLine($"\n  [+] My permissions bitmask: {p.Permissions}, roles: [{string.Join(", ", p.RoleIds)}]");
connection.ChannelOverridesReceived += r =>
{
    Console.WriteLine("\n  --- channel overrides ---");
    if (r.Overrides.Count == 0) Console.WriteLine("  (none)");
    foreach (var o in r.Overrides) Console.WriteLine($"  roleId={o.RoleId} allow={o.Allow} deny={o.Deny}");
};
connection.SetChannelOverrideResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.DeleteChannelOverrideResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.RoleListReceived += r =>
{
    Console.WriteLine("\n  --- roles ---");
    foreach (var role in r.Roles)
        Console.WriteLine($"  [{role.Id}] {role.Name}{(role.IsSystem ? " (system)" : "")} permissions={role.Permissions}");
};
connection.RoleCreateResponseReceived += r => Console.WriteLine(r.IsSuccess
    ? $"\n  [+] Role created, roleId={r.RoleId}"
    : $"\n  [!] Role create failed, status={r.Status}");
connection.RoleUpdateResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.RoleDeleteResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.RoleAssignResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.RoleRemoveResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.ChannelKickResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.ChannelUnbanResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.ChannelMuteResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.ChannelUnmuteResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.ChannelKicked += p => Console.WriteLine($"\n  [!] You were kicked from room {p.RoomId}");
connection.IpBanListReceived += r =>
{
    Console.WriteLine("\n  --- banned IPs ---");
    if (r.Ips.Count == 0) Console.WriteLine("  (none)");
    foreach (var ip in r.Ips) Console.WriteLine($"  {ip}");
};
connection.IpBanResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");
connection.IpUnbanResponseReceived += r => Console.WriteLine(r.IsSuccess ? "\n  [+] OK" : $"\n  [!] Failed, status={r.Status}");

static string Describe(byte status) => status switch
{
    0 => "OK",
    1 => "room not found",
    2 => "membership conflict",
    253 => "banned from this channel",
    254 => "insufficient permissions",
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
    Console.WriteLine("10-List roles  11-Create role  12-Update role  13-Delete role  14-Assign role  15-Remove role");
    Console.WriteLine("16-Get channel overrides  17-Set channel override  18-Delete channel override");
    Console.WriteLine("19-Kick from channel  20-Unban from channel  21-Mute in channel  22-Unmute in channel");
    Console.WriteLine("23-List banned IPs  24-Ban IP  25-Unban IP");
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

        case "10":
            await connection.ListRolesAsync();
            break;

        case "11":
            Console.Write("  Role name: "); var roleName = Console.ReadLine() ?? "";
            Console.Write("  Permissions (bitmask): ");
            uint.TryParse(Console.ReadLine(), out var createPerms);
            await connection.CreateRoleAsync(roleName, createPerms);
            break;

        case "12":
            Console.Write("  Role id: ");
            if (long.TryParse(Console.ReadLine(), out var updateRoleId))
            {
                Console.Write("  New name: "); var newName = Console.ReadLine() ?? "";
                Console.Write("  New permissions (bitmask): ");
                uint.TryParse(Console.ReadLine(), out var newPerms);
                await connection.UpdateRoleAsync(updateRoleId, newName, newPerms);
            }
            break;

        case "13":
            Console.Write("  Role id: ");
            if (long.TryParse(Console.ReadLine(), out var deleteRoleId))
                await connection.DeleteRoleAsync(deleteRoleId);
            break;

        case "14":
        case "15":
            Console.Write("  User id: ");
            if (long.TryParse(Console.ReadLine(), out var membershipUserId))
            {
                Console.Write("  Role id: ");
                if (long.TryParse(Console.ReadLine(), out var membershipRoleId))
                {
                    if (input == "14") await connection.AssignRoleAsync(membershipUserId, membershipRoleId);
                    else await connection.RemoveRoleAsync(membershipUserId, membershipRoleId);
                }
            }
            break;

        case "16":
            Console.Write("  Room id: ");
            if (long.TryParse(Console.ReadLine(), out var overridesRoomId))
                await connection.GetChannelOverridesAsync(overridesRoomId);
            break;

        case "17":
            Console.Write("  Room id: ");
            if (long.TryParse(Console.ReadLine(), out var setOverrideRoomId))
            {
                Console.Write("  Role id: ");
                if (long.TryParse(Console.ReadLine(), out var setOverrideRoleId))
                {
                    Console.Write("  Allow (bitmask): "); uint.TryParse(Console.ReadLine(), out var allow);
                    Console.Write("  Deny (bitmask): "); uint.TryParse(Console.ReadLine(), out var deny);
                    await connection.SetChannelOverrideAsync(setOverrideRoomId, setOverrideRoleId, allow, deny);
                }
            }
            break;

        case "18":
            Console.Write("  Room id: ");
            if (long.TryParse(Console.ReadLine(), out var delOverrideRoomId))
            {
                Console.Write("  Role id: ");
                if (long.TryParse(Console.ReadLine(), out var delOverrideRoleId))
                    await connection.DeleteChannelOverrideAsync(delOverrideRoomId, delOverrideRoleId);
            }
            break;

        case "19":
        case "20":
        case "21":
        case "22":
            Console.Write("  Room id: ");
            if (long.TryParse(Console.ReadLine(), out var modRoomId))
            {
                Console.Write("  User id: ");
                if (long.TryParse(Console.ReadLine(), out var modUserId))
                {
                    switch (input)
                    {
                        case "19": await connection.KickFromChannelAsync(modRoomId, modUserId); break;
                        case "20": await connection.UnbanFromChannelAsync(modRoomId, modUserId); break;
                        case "21": await connection.MuteInChannelAsync(modRoomId, modUserId); break;
                        case "22": await connection.UnmuteInChannelAsync(modRoomId, modUserId); break;
                    }
                }
            }
            break;

        case "23":
            await connection.ListBannedIpsAsync();
            break;

        case "24":
            Console.Write("  IP: ");
            await connection.BanIpAsync(Console.ReadLine() ?? "");
            break;

        case "25":
            Console.Write("  IP: ");
            await connection.UnbanIpAsync(Console.ReadLine() ?? "");
            break;
    }

    await Task.Delay(200); // даём ответу успеть напечататься до перерисовки меню
}

await connection.DisposeAsync();
Console.WriteLine("[client] Bye");