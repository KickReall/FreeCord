using System;
using System.Linq;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using FreeCord.Protocol;

namespace FreeCord.Desktop.ViewModels;

/// <summary>
/// Состояние одного подключённого (или ещё не подключённого) сервера — своё
/// соединение, свой список комнат, своя лента сообщений. Один экземпляр живёт,
/// пока приложение открыто, независимо от того, какой сервер сейчас активен
/// в левой панели — переключение между серверами не разрывает остальные.
/// </summary>
public partial class ServerSessionViewModel : ViewModelBase
{
    private readonly FreeCordConnection _connection = new();
    private readonly ServerListStore _store;
    private readonly LocalNicknameStore _nicknames = new();

    // Последние полученные "сырые" списки — держим отдельно от MemberGroups, т.к.
    // группировку нужно пересобирать заново при обновлении любого из двух.
    private IReadOnlyList<RoleInfo> _rolesRaw = Array.Empty<RoleInfo>();
    private IReadOnlyList<UserInfo> _usersRaw = Array.Empty<UserInfo>();
    // Проверки на дубликат среди остальных вкладок — держатся на стороне
    // MainWindowViewModel, т.к. только он видит полный список серверов.
    // _guardConnect — дешёвая проверка по host:port до подключения к сети.
    // _guardFingerprint — надёжная проверка по факту: один физический сервер может
    // быть доступен под разными адресами (127.0.0.1 и 127.0.0.2 на loopback —
    // это ровно тот случай, который поймали вживую), а TLS-сертификат у него один.
    private readonly Func<ServerSessionViewModel, bool> _guardConnect;
    private readonly Func<ServerSessionViewModel, bool> _guardFingerprint;

    // Отпечаток для проверки на дубликат (в интерфейсе не показывается). Если сессия
    // ещё не подключалась в этом запуске приложения — берём то, что уже закреплено
    // (TOFU) для этого host:port с прошлого раза, а не ждём нового подключения:
    // иначе коллизию с ранее сохранённой, но ещё не тронутой в этой сессии вкладкой
    // просто нечем было бы обнаружить (наблюдалось на практике: после перезапуска
    // 127.0.0.2 успешно логинился, потому что отпечаток 127.0.0.1 был "неизвестен").
    public string? ServerFingerprint => _connection.ServerFingerprint ?? _connection.GetPinnedFingerprint(Host, Port);

    // true, если этот сервер уже когда-то сохранён в servers.json —
    // чтобы не писать дубликат при повторном успешном логине, и чтобы
    // MainWindowViewModel мог отличить настоящую вкладку от свежедобавленной пустой.
    public bool IsPersisted { get; private set; }

    [ObservableProperty] private string _host;
    [ObservableProperty] private int _port;

    // Переключатель "адрес vs инвайт-ссылка" на панели логина — оба поля независимы,
    // видно только одно в зависимости от UseInviteLink (см. MainWindow.axaml).
    [ObservableProperty] private bool _useInviteLink;
    [ObservableProperty] private string _inviteLinkText = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Initials))]
    private string _displayName;
    [ObservableProperty] private string _username = "";
    [ObservableProperty] private string _password = "";
    [ObservableProperty] private string _status = "Not connected";

    // IsLoggedIn управляет тем, какая панель видна: логин или чат
    [ObservableProperty] private bool _isLoggedIn;

    // userId залогиненного — нужен, чтобы в панели участников скрыть у самого себя
    // переименование/выдачу роли/блокировку (админу нет смысла делать это себе).
    public long CurrentUserId { get; private set; }

    [ObservableProperty] private string _newRoomName = "";
    [ObservableProperty] private string _messageText = "";
    [ObservableProperty] private RoomInfo? _selectedRoom;

    public ObservableCollection<RoomInfo> Rooms { get; } = new();
    public ObservableCollection<MessageItem> Messages { get; } = new();

    // Панель участников: список ролей (тоже нужен для подменю "Выдать роль")
    // и итоговая группировка пользователей по ролям.
    public ObservableCollection<RoleInfo> Roles { get; } = new();
    public ObservableCollection<RoleGroupViewModel> MemberGroups { get; } = new();

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanManageRoles))]
    [NotifyPropertyChangedFor(nameof(CanManageServerBans))]
    private uint _permissions;

    public bool CanManageRoles => (Permissions & (uint)Permission.ManageRoles) != 0;
    public bool CanManageServerBans => (Permissions & (uint)Permission.ManageServerBans) != 0;

    // Диалоги — забота View: открытие окна переименования/выбора роли делает
    // MainWindow.axaml.cs в ответ на эти события, как и с RenameRequested у серверов.
    public event Action<MemberViewModel>? MemberRenameRequested;
    public event Action<MemberViewModel>? MemberAssignRoleRequested;

    // Инициалы для кружка в левой панели, пока у серверов нет своих иконок
    public string Initials => string.IsNullOrWhiteSpace(DisplayName)
        ? "?"
        : string.Concat(DisplayName.Split(' ', StringSplitOptions.RemoveEmptyEntries)
            .Take(2).Select(w => char.ToUpperInvariant(w[0])));

    public ServerSessionViewModel(ServerListStore store, string host, int port, string displayName, bool isPersisted,
        Func<ServerSessionViewModel, bool> guardConnect, Func<ServerSessionViewModel, bool> guardFingerprint)
    {
        _store = store;
        _guardConnect = guardConnect;
        _guardFingerprint = guardFingerprint;
        _host = host;
        _port = port;
        _displayName = displayName;
        IsPersisted = isPersisted;

        // ВАЖНО: события приходят из фонового потока приёма.
        // Трогать ObservableCollection можно только из UI-потока, иначе падение.
        _connection.AuthResponseReceived += r => OnUi(() =>
        {
            if (r.IsSuccess)
            {
                IsLoggedIn = true;
                CurrentUserId = r.UserId;
                Status = $"Logged in as {Username} (id={r.UserId})";
                PersistIfNeeded();
                _ = _connection.ListRoomsAsync();
                _ = _connection.ListRolesAsync();
                _ = _connection.ListUsersAsync();
            }
            else Status = "Login failed: wrong username or password";
        });

        _connection.RegisterResponseReceived += r => OnUi(() =>
            Status = r.IsSuccess ? "Registered, now log in" : "Registration failed: username taken");

        _connection.UserRegistered += u => OnUi(() =>
        {
            Status = $"Новый пользователь: {u.Username}";
            // Список участников уже мог быть загружен раньше, чем зарегистрировался этот
            // пользователь — обновляем, чтобы он сразу появился в панели.
            _ = _connection.ListUsersAsync();
        });

        _connection.MyPermissionsReceived += p => OnUi(() => Permissions = p.Permissions);

        _connection.RoleListReceived += r => OnUi(() =>
        {
            _rolesRaw = r.Roles;
            Roles.Clear();
            foreach (var role in r.Roles) Roles.Add(role);
            RebuildMemberGroups();
        });

        _connection.UserListReceived += r => OnUi(() =>
        {
            _usersRaw = r.Users;
            RebuildMemberGroups();
        });

        _connection.RoleAssignResponseReceived += r => OnUi(() =>
        {
            Status = r.IsSuccess ? "Роль выдана" : "Не удалось выдать роль";
            if (r.IsSuccess) _ = _connection.ListUsersAsync();
        });

        _connection.BanUserSessionResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Пользователь заблокирован",
                1 => "Пользователь сейчас не в сети",
                2 => "Нельзя заблокировать самого себя",
                254 => "Недостаточно прав",
                _ => "Ошибка блокировки"
            });

        _connection.RoomCreated += r => OnUi(() =>
        {
            // Защита от дубликата: список мог обновиться другим путём
            if (Rooms.Any(room => room.Id == r.RoomId)) return;
            Rooms.Add(new RoomInfo(r.RoomId, r.Name));
        });

        _connection.RoomListReceived += r => OnUi(() =>
        {
            Rooms.Clear();
            foreach (var room in r.Rooms) Rooms.Add(room);
        });

        _connection.RoomCreateResponseReceived += r => OnUi(() =>
        {
            Status = r.IsSuccess ? $"Room created (id={r.RoomId})" : "Room name already taken";
            if (r.IsSuccess) _ = _connection.ListRoomsAsync();
        });

        _connection.JoinResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Joined room",
                1 => "Room not found",
                2 => "Already a member",
                _ => $"Join error {r.Status}"
            });

        _connection.HistoryReceived += r => OnUi(() =>
        {
            Messages.Clear();
            foreach (var m in r.Messages)
                Messages.Add(new MessageItem(m.SenderName, m.Text, FormatTime(m.Timestamp)));
        });

        _connection.MessageReceived += m => OnUi(() =>
        {
            if (m.RoomId != SelectedRoom?.Id) return;
            Messages.Add(new MessageItem(m.SenderName, m.Text, FormatTime(m.Timestamp)));
        });

        _connection.Disconnected += ex => OnUi(() =>
        {
            IsLoggedIn = false;
            Status = $"Disconnected: {ex?.Message ?? "connection closed"}";
        });
    }

    private void PersistIfNeeded()
    {
        if (IsPersisted) return;
        IsPersisted = true;
        var entries = _store.Load();
        entries.Add(new ServerEntry(Host, Port, DisplayName));
        _store.Save(entries);
    }

    // Переименование уже сохранённого сервера (через поле "Название" или
    // контекстное меню в рейле) сразу отражается в servers.json.
    partial void OnDisplayNameChanged(string value)
    {
        if (!IsPersisted) return;
        var entries = _store.Load();
        int index = entries.FindIndex(e =>
            string.Equals(e.Host, Host, StringComparison.OrdinalIgnoreCase) && e.Port == Port);
        if (index < 0) return;
        entries[index] = entries[index] with { DisplayName = value };
        _store.Save(entries);
    }

    // Пересобирает группировку по ролям заново из последних полученных списков.
    // Пользователь показывается под КАЖДОЙ своей ролью — иерархии ролей в проекте
    // нет (см. CLAUDE.md), поэтому "основную" роль выделить нечем и незачем.
    private void RebuildMemberGroups()
    {
        MemberGroups.Clear();
        if (_rolesRaw.Count == 0 || _usersRaw.Count == 0) return;

        foreach (var role in _rolesRaw)
        {
            var group = new RoleGroupViewModel(role.DisplayName);
            foreach (var user in _usersRaw)
            {
                if (!user.RoleIds.Contains(role.Id)) continue;
                group.Members.Add(CreateMemberViewModel(user));
            }
            if (group.Members.Count > 0) MemberGroups.Add(group);
        }

        // На практике не бывает — CreateUser всегда назначает admin либо guest —
        // но не полагаемся на это молча, если данные когда-нибудь разойдутся.
        var withoutRole = _usersRaw.Where(u => u.RoleIds.Count == 0).ToList();
        if (withoutRole.Count > 0)
        {
            var group = new RoleGroupViewModel("Без роли");
            foreach (var user in withoutRole)
                group.Members.Add(CreateMemberViewModel(user));
            MemberGroups.Add(group);
        }
    }

    private MemberViewModel CreateMemberViewModel(UserInfo user) => new(
        user.Id, user.Username, _nicknames.Get(Host, Port, user.Id),
        isSelf: user.Id == CurrentUserId,
        canManageRoles: CanManageRoles,
        canManageServerBans: CanManageServerBans,
        block: BlockUser);

    [RelayCommand]
    private void RenameMember(MemberViewModel member) => MemberRenameRequested?.Invoke(member);

    [RelayCommand]
    private void AssignRoleToMember(MemberViewModel member) => MemberAssignRoleRequested?.Invoke(member);

    // Пустая строка сбрасывает локальный никнейм обратно к настоящему имени.
    public void SetLocalNickname(long userId, string nickname)
    {
        string? stored = string.IsNullOrEmpty(nickname) ? null : nickname;
        _nicknames.Set(Host, Port, userId, stored);

        var member = MemberGroups.SelectMany(g => g.Members).FirstOrDefault(m => m.UserId == userId);
        if (member is not null) member.LocalNickname = stored;
    }

    public Task AssignRoleAsync(long userId, long roleId) => _connection.AssignRoleAsync(userId, roleId);

    private void BlockUser(long userId) => _ = _connection.BanUserSessionAsync(userId);

    // Вызывается MainWindowViewModel при удалении сервера из рейла —
    // закрывает соединение, чтобы фоновый поток приёма не остался висеть.
    public async Task DisconnectAsync()
    {
        await _connection.DisposeAsync();
    }

    // Инвайт-ссылка (freecord://host:port?fp=...) разбирается в Host/Port сразу по мере
    // ввода/вставки — к моменту нажатия Login эти поля уже содержат нужные значения.
    // Отпечаток, если он был в ссылке, закрепляем заранее (см. PinFingerprintIfUnknown).
    partial void OnInviteLinkTextChanged(string value)
    {
        if (!InviteLink.TryParse(value, out var host, out var port, out var fingerprint)) return;

        Host = host;
        Port = port;
        if (fingerprint is not null) _connection.PinFingerprintIfUnknown(host, port, fingerprint);
    }

    private static void OnUi(Action action) => Dispatcher.UIThread.Post(action);
    // Сервер отдаёт unix-время в секундах, переводим в локальное время пользователя
    private static string FormatTime(long unixSeconds) =>
        DateTimeOffset.FromUnixTimeSeconds(unixSeconds).ToLocalTime().ToString("HH:mm");

    private async Task EnsureConnectedAsync()
    {
        if (_connection.IsConnected) return;
        Status = "Connecting...";
        await _connection.ConnectAsync(Host, Port);
        Status = "Connected";
    }

    [RelayCommand]
    private async Task LoginAsync()
    {
        if (!_guardConnect(this)) return;
        try
        {
            await EnsureConnectedAsync();
            if (!_guardFingerprint(this)) return;
            await _connection.LoginAsync(Username, Password);
        }
        catch (Exception ex) { Status = $"Error: {ex.Message}"; }
    }

    [RelayCommand]
    private async Task RegisterAsync()
    {
        if (!_guardConnect(this)) return;
        try
        {
            await EnsureConnectedAsync();
            if (!_guardFingerprint(this)) return;
            await _connection.RegisterAsync(Username, Password);
        }
        catch (Exception ex) { Status = $"Error: {ex.Message}"; }
    }

    [RelayCommand]
    private async Task CreateRoomAsync()
    {
        if (string.IsNullOrWhiteSpace(NewRoomName)) return;
        await _connection.CreateRoomAsync(NewRoomName);
        NewRoomName = "";
    }

    [RelayCommand]
    private async Task JoinSelectedRoomAsync()
    {
        if (SelectedRoom is null) return;
        await _connection.JoinRoomAsync(SelectedRoom.Id);
    }

    [RelayCommand]
    private async Task SendMessageAsync()
    {
        if (SelectedRoom is null || string.IsNullOrWhiteSpace(MessageText)) return;
        await _connection.SendTextAsync(SelectedRoom.Id, MessageText);
        MessageText = "";
    }

    // Срабатывает при клике по комнате в списке
    partial void OnSelectedRoomChanged(RoomInfo? value)
    {
        Messages.Clear();

        if (value is null)
        {
            _ = _connection.LeaveRoomAsync(0);
            return;
        }

        _ = SwitchToRoomAsync(value.Id);
    }

    private async Task SwitchToRoomAsync(long roomId)
    {
        await _connection.JoinRoomAsync(roomId);
        await _connection.RequestHistoryAsync(roomId);
    }
}

// Одно сообщение в ленте — с автором и временем
public sealed record MessageItem(string Author, string Text, string Time);
