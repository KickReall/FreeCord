using System;
using System.Linq;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Media.Imaging;
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
    private readonly AvatarCache _avatars = new();

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

    // Автопереподключение при обрыве связи (не по инициативе пользователя) — см.
    // StartReconnectLoop. _userInitiatedDisconnect ставится только в DisconnectAsync
    // (сервер убрали из рейла), чтобы не пытаться переподключаться к тому, что
    // пользователь сам закрыл.
    private CancellationTokenSource? _reconnectCts;
    private bool _userInitiatedDisconnect;

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
    // Плоское, а не [ObservableProperty]: значение меняется только в PersistIfNeeded,
    // которое само шлёт уведомление — там же, где это в принципе может произойти.
    public bool IsPersisted { get; private set; }

    [ObservableProperty] private string _host;
    [ObservableProperty] private int _port;

    // Переключатель "адрес vs инвайт-ссылка" на панели логина — оба поля независимы,
    // видно только одно в зависимости от UseInviteLink (см. MainWindow.axaml).
    [ObservableProperty] private bool _useInviteLink;
    [ObservableProperty] private string _inviteLinkText = "";

    // Глобальное — задаёт владелец во вкладке "Информация о сервере" (см. ServerSettingsWindow),
    // приходит с сервера (ServerInfoResponse) и до, и после логина; локального
    // переименования больше нет. servers.json кэширует последнее известное значение,
    // чтобы в рейле было что показать ещё до подключения (см. OnDisplayNameChanged).
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Initials))]
    private string _displayName;
    [ObservableProperty] private string _description = "";
    [ObservableProperty] private string _username = "";
    [ObservableProperty] private string _password = "";
    [ObservableProperty] private string _status = "Not connected";

    // IsLoggedIn управляет тем, какая панель видна: логин или чат
    [ObservableProperty] private bool _isLoggedIn;

    // userId залогиненного — нужен, чтобы в панели участников скрыть у самого себя
    // переименование/выдачу роли/блокировку (админу нет смысла делать это себе).
    public long CurrentUserId { get; private set; }

    // Список ролей самого залогиненного (из MyPermissions) — нужен только чтобы
    // определить, Owner ли текущий пользователь: кнопка настроек сервера видна
    // только ему, независимо от прав (Owner — про неприкосновенность, не про биты).
    private IReadOnlyList<long> _myRoleIds = Array.Empty<long>();
    public bool IsOwner => _myRoleIds.Contains(RoleIds.Owner);

    [ObservableProperty] private string _newRoomName = "";
    [ObservableProperty] private string _messageText = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelectedRoom))]
    [NotifyPropertyChangedFor(nameof(CanSendInSelectedRoom))]
    [NotifyPropertyChangedFor(nameof(MessageInputPlaceholder))]
    private RoomInfo? _selectedRoom;

    // Поле ввода и кнопка отправки видны, только пока открыта комната — писать
    // "в никуда" бессмысленно, а раньше поле оставалось видно даже без выбранной комнаты.
    public bool HasSelectedRoom => SelectedRoom is not null;

    // RoomInfo.CanSendMessages — это уже готовый результат EffectivePermissionsInRoom с
    // сервера (системная комната, оверрайды по каналам), но пересчитывается только при
    // каждом запросе списка комнат; глобальный битовый флаг Permissions обновляется отдельно
    // и сразу же (RefreshPermissionsIfOnline на gateway после выдачи/снятия роли), поэтому
    // проверяем оба — иначе после смены роли поле осталось бы активным ещё до переподключения.
    public bool CanSendInSelectedRoom =>
        SelectedRoom is { CanSendMessages: true } && (Permissions & (uint)Permission.SendMessages) != 0;

    // Раньше поле ввода оставалось видно и активно даже без права писать — пользователь
    // узнавал о запрете только по тому, что сообщение молча не появлялось в ленте.
    public string MessageInputPlaceholder => CanSendInSelectedRoom
        ? "Написать сообщение..."
        : "Нет прав писать в этот канал";

    public ObservableCollection<RoomInfo> Rooms { get; } = new();

    // Задел на будущее (см. CLAUDE.md, план "Голос и видео") — Rooms остаётся единым
    // источником правды (используется, например, для дедупликации в RoomCreated),
    // а эти две коллекции — чисто для отображения списка двумя секциями в UI.
    public ObservableCollection<RoomInfo> TextRooms { get; } = new();
    public ObservableCollection<RoomInfo> VoiceRooms { get; } = new();

    public ObservableCollection<MessageItem> Messages { get; } = new();

    // Панель участников: список ролей (тоже нужен для подменю "Выдать роль")
    // и итоговая группировка пользователей по ролям.
    public ObservableCollection<RoleInfo> Roles { get; } = new();
    public ObservableCollection<RoleGroupViewModel> MemberGroups { get; } = new();

    // Плоский список всех участников — для вкладки "Пользователи" в настройках сервера
    // (там нужна таблица, а не группировка по одной главной роли, как в MemberGroups).
    public ObservableCollection<MemberViewModel> AllMembers { get; } = new();

    // Чёрный список IP — тоже для вкладки "Пользователи", подгружается по запросу
    // (см. ServerSettingsViewModel.SelectUsersTab), а не сразу при логине, как роли/участники.
    public ObservableCollection<string> BannedIps { get; } = new();

    // Оверрайды прав по ролям для канала, который сейчас открыт во вкладке "Каналы" —
    // подгружаются по запросу конкретного roomId (см. GetChannelOverridesAsync).
    // ChannelOverrideChanged сообщает наружу об успешном изменении, не пытаясь само
    // угадать, какой канал сейчас выбран в UI — это забота ServerSettingsViewModel.
    public ObservableCollection<ChannelOverrideInfo> ChannelOverrides { get; } = new();
    public event Action? ChannelOverrideChanged;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CanManageRoles))]
    [NotifyPropertyChangedFor(nameof(CanManageServerBans))]
    [NotifyPropertyChangedFor(nameof(CanManageChannel))]
    [NotifyPropertyChangedFor(nameof(CanManageUsers))]
    [NotifyPropertyChangedFor(nameof(CanManageServer))]
    [NotifyPropertyChangedFor(nameof(CanManageMessages))]
    [NotifyPropertyChangedFor(nameof(CanManageFiles))]
    [NotifyPropertyChangedFor(nameof(CanAccessUsersTab))]
    [NotifyPropertyChangedFor(nameof(CanOpenServerSettings))]
    [NotifyPropertyChangedFor(nameof(CanSendInSelectedRoom))]
    [NotifyPropertyChangedFor(nameof(MessageInputPlaceholder))]
    private uint _permissions;

    public bool CanManageRoles => (Permissions & (uint)Permission.ManageRoles) != 0;
    public bool CanManageServerBans => (Permissions & (uint)Permission.ManageServerBans) != 0;
    public bool CanManageChannel => (Permissions & (uint)Permission.ManageChannel) != 0;
    public bool CanManageUsers => (Permissions & (uint)Permission.ManageUsers) != 0;
    public bool CanManageServer => (Permissions & (uint)Permission.ManageServer) != 0;
    public bool CanManageMessages => (Permissions & (uint)Permission.ManageMessages) != 0;
    public bool CanManageFiles => (Permissions & (uint)Permission.ManageFiles) != 0;

    // Вкладка "Пользователи" показывает и назначение ролей, и блокировку/удаление,
    // и список забаненных IP — видна, если есть хоть одно из соответствующих прав.
    public bool CanAccessUsersTab => CanManageUsers || CanManageServerBans || CanManageRoles;

    // Кнопка-шестерёнка в шапке — по любому из прав на настройки сервера, а не только
    // Owner'у (тот всё равно admin и потому уже проходит эту проверку без исключений).
    // Голос/Видео не участвуют — это заглушки на вторую версию, без своего права.
    public bool CanOpenServerSettings =>
        CanManageServer || CanManageRoles || CanAccessUsersTab || CanManageChannel || CanManageMessages || CanManageFiles;

    // Иконка сервера — одна на весь деплой, приходит пушем (см. ServerIconFetchResponse)
    // и сразу после логина. null — иконки нет или ещё не подгрузилась, тогда в UI
    // остаются инициалы, как раньше.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasServerIcon))]
    private Bitmap? _serverIcon;

    public bool HasServerIcon => ServerIcon is not null;

    private string ServerIconCacheKey => $"{Host}:{Port}:servericon";

    // Permissions может смениться и без перелогина (см. RefreshPermissionsIfOnline на
    // gateway, после того как кто-то выдал/убрал роль) — но уже построенные MemberViewModel
    // держат Can*-флаги статичным снимком с момента создания, поэтому без пересборки
    // контекстное меню оставалось бы с правами, актуальными на момент login, до перезапуска.
    partial void OnPermissionsChanged(uint value) => RebuildMemberGroups();

    // Показ/скрытие панели участников — чисто клиентское состояние, серверу не известно.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(MembersToggleGlyph))]
    private bool _showMembers = true;

    // Стрелка на узкой полоске-разделителе между лентой и панелью участников —
    // направление подсказывает, что случится по клику (показать/скрыть).
    public string MembersToggleGlyph => ShowMembers ? "›" : "‹";

    // "X печатает..." под лентой сообщений — сбрасывается при смене комнаты
    // (OnSelectedRoomChanged) и по таймауту на каждого печатающего отдельно.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasTypingIndicator))]
    private string _typingIndicatorText = "";
    public bool HasTypingIndicator => !string.IsNullOrEmpty(TypingIndicatorText);

    private readonly Dictionary<long, (string Name, DateTime Expiry)> _typingUsers = new();
    private DateTime _lastTypingSentUtc = DateTime.MinValue;

    // Диалог переименования — забота View, MainWindow.axaml.cs открывает его в ответ
    // на это событие. Выдача/снятие роли, блокировка, удаление и загрузка своей
    // аватарки убраны из контекстного меню участника целиком — переезжают в окно
    // настроек сервера (вкладка "Пользователи") и в будущий экран настроек профиля;
    // сетевые обёртки (AssignRoleAsync и т.п. ниже) остаются, их вызовет уже новый UI.
    public event Action<MemberViewModel>? MemberRenameRequested;

    // Открытие окна настроек сервера — кнопка-шестерёнка в шапке списка каналов,
    // видна только Owner'у (см. IsOwner). MainWindow.axaml.cs открывает окно в ответ.
    public event Action<ServerSessionViewModel>? ServerSettingsRequested;

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
                _ = _connection.FetchServerIconAsync();
                // После реконнекта комната, что была открыта, теряется на сервере
                // (currentRoomId живёт в сессии, а не в БД) — открываем её заново и
                // подтягиваем свежую историю на случай пропущенных сообщений.
                if (SelectedRoom is not null) _ = SwitchToRoomAsync(SelectedRoom.Id);
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

        _connection.MyPermissionsReceived += p => OnUi(() =>
        {
            Permissions = p.Permissions;
            _myRoleIds = p.RoleIds;
            OnPropertyChanged(nameof(IsOwner));
        });

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

        _connection.RoleRemoveResponseReceived += r => OnUi(() =>
        {
            Status = r.IsSuccess ? "Роль убрана" : "Не удалось убрать роль";
            if (r.IsSuccess) _ = _connection.ListUsersAsync();
        });

        // CRUD самой роли (не путать с выдачей/снятием существующей роли пользователю
        // выше) — используется вкладкой "Роли" в настройках сервера.
        _connection.RoleCreateResponseReceived += r => OnUi(() =>
        {
            Status = r.Status switch
            {
                0 => "Роль создана",
                1 => "Имя роли уже занято",
                254 => "Недостаточно прав",
                _ => "Не удалось создать роль"
            };
            if (r.IsSuccess) _ = _connection.ListRolesAsync();
        });

        _connection.RoleUpdateResponseReceived += r => OnUi(() =>
        {
            Status = r.Status switch
            {
                0 => "Роль обновлена",
                1 => "Роль не найдена",
                2 => "Это системная роль — нельзя менять техническое имя",
                3 => "Имя роли уже занято",
                254 => "Недостаточно прав",
                _ => "Не удалось обновить роль"
            };
            if (r.Status == 0) _ = _connection.ListRolesAsync();
        });

        _connection.RoleDeleteResponseReceived += r => OnUi(() =>
        {
            Status = r.Status switch
            {
                0 => "Роль удалена",
                1 => "Роль не найдена",
                2 => "Системную роль нельзя удалить",
                254 => "Недостаточно прав",
                _ => "Не удалось удалить роль"
            };
            if (r.Status == 0) _ = _connection.ListRolesAsync();
        });

        _connection.BanUserSessionResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Пользователь заблокирован",
                1 => "Пользователь сейчас не в сети",
                2 => "Нельзя заблокировать самого себя",
                3 => "Нельзя заблокировать владельца сервера",
                254 => "Недостаточно прав",
                _ => "Ошибка блокировки"
            });

        // Успех ничего не досылает сам — сервер после удаления рассылает свежий
        // UserListResponse всем сам (BroadcastUserListToAll на gateway), в том числе инициатору.
        _connection.DeleteUserResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Аккаунт удалён",
                1 => "Пользователь не найден",
                2 => "Нельзя удалить самого себя",
                3 => "Нельзя удалить владельца сервера",
                254 => "Недостаточно прав",
                _ => "Ошибка удаления"
            });

        _connection.IpBanListReceived += r => OnUi(() =>
        {
            BannedIps.Clear();
            foreach (var ip in r.Ips) BannedIps.Add(ip);
        });

        _connection.IpBanResponseReceived += r => OnUi(() =>
        {
            Status = r.IsSuccess ? "IP заблокирован" : "Не удалось заблокировать IP";
            if (r.IsSuccess) _ = _connection.ListBannedIpsAsync();
        });

        _connection.IpUnbanResponseReceived += r => OnUi(() =>
        {
            Status = r.IsSuccess ? "IP разблокирован" : "Не удалось разблокировать IP";
            if (r.IsSuccess) _ = _connection.ListBannedIpsAsync();
        });

        // Сервер после успешной загрузки сам рассылает свежий UserListResponse с новой
        // версией всем (см. BroadcastUserListToAll в HandleAvatarUpload) — этот статус
        // только для обратной связи по самой попытке, обновление байтов придёт отдельно.
        _connection.AvatarUploadResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Аватарка обновлена",
                1 => "Файл слишком большой",
                2 => "Сервис недоступен",
                _ => "Ошибка загрузки аватарки"
            });

        _connection.AvatarFetchResponseReceived += r => OnUi(() =>
        {
            if (r.Version == 0) return;  // сервер ответил "аватарки нет" — кэшировать нечего
            var bitmap = _avatars.Save(AvatarCacheKey(r.UserId), r.Version, r.Data);
            if (bitmap is null) return;
            foreach (var member in MemberGroups.SelectMany(g => g.Members).Where(m => m.UserId == r.UserId))
                member.Avatar = bitmap;
        });

        _connection.ServerIconUploadResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Иконка сервера обновлена",
                1 => "Файл слишком большой",
                2 => "Не удалось сохранить на сервере",
                254 => "Недостаточно прав",
                _ => "Ошибка загрузки иконки"
            });

        // Тот же обработчик и на явный ответ, и на пуш всем при чужой успешной загрузке —
        // содержимое самодостаточно (version+bytes), различать источник не нужно.
        _connection.ServerIconFetchResponseReceived += r => OnUi(() =>
            ServerIcon = r.Version == 0 ? null : _avatars.Save(ServerIconCacheKey, r.Version, r.Data));

        // Тот же обработчик и на явный ответ, и на пуш всем при изменении владельцем —
        // содержимое самодостаточно, различать источник не нужно (как с иконкой).
        _connection.ServerInfoResponseReceived += r => OnUi(() =>
        {
            DisplayName = string.IsNullOrWhiteSpace(r.Name) ? $"{Host}:{Port}" : r.Name;
            Description = r.Description;
        });

        _connection.SetServerInfoResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Информация о сервере обновлена",
                1 => "Слишком длинное значение",
                254 => "Недостаточно прав",
                _ => "Ошибка сохранения"
            });

        _connection.TypingReceived += t => OnUi(() =>
        {
            if (t.RoomId != SelectedRoom?.Id) return;
            string name = _nicknames.Get(Host, Port, t.SenderId) ?? t.SenderName;
            _typingUsers[t.SenderId] = (name, DateTime.UtcNow.AddSeconds(5));
            RefreshTypingIndicator();
            _ = Task.Delay(TimeSpan.FromSeconds(5)).ContinueWith(_ => OnUi(RefreshTypingIndicator));
        });

        _connection.RoomCreated += r => OnUi(() =>
        {
            // Защита от дубликата: список мог обновиться другим путём
            if (Rooms.Any(room => room.Id == r.RoomId)) return;
            AddRoom(new RoomInfo(r.RoomId, r.Name, r.Type));
        });

        // Переименование/удаление — правит тот же Rooms (и TextRooms/VoiceRooms) без
        // Clear()+пересборки, той же ценой, какой уже стоила потеря выделения комнаты
        // при обычном обновлении списка (см. SyncRooms) — точечно, чтобы не задеть
        // SelectedRoom, если сейчас открыта другая комната.
        _connection.RoomUpdated += r => OnUi(() =>
        {
            var room = Rooms.FirstOrDefault(x => x.Id == r.RoomId);
            if (room is null) return;
            var updated = room with { Name = r.Name };
            ReplaceRoom(room, updated);
        });

        _connection.RoomUpdateResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Канал переименован",
                1 => "Канал не найден",
                2 => "Название уже занято",
                3 => "Нельзя переименовать системный канал",
                254 => "Недостаточно прав",
                _ => "Не удалось переименовать канал"
            });

        _connection.RoomDeleted += r => OnUi(() =>
        {
            var room = Rooms.FirstOrDefault(x => x.Id == r.RoomId);
            if (room is null) return;
            Rooms.Remove(room);
            TextRooms.Remove(room);
            VoiceRooms.Remove(room);
        });

        _connection.RoomDeleteResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Канал удалён",
                1 => "Канал не найден",
                2 => "Нельзя удалить системный канал",
                254 => "Недостаточно прав",
                _ => "Не удалось удалить канал"
            });

        // Кик из канала (и, тем же путём, принудительный выход при удалении канала —
        // см. ForceLeaveRoomForAll на gateway) — раньше событие вообще не слушалось,
        // хотя сервер его уже слал: UI для настоящего кика просто не существовало.
        _connection.ChannelKicked += n => OnUi(() =>
        {
            if (n.RoomId != SelectedRoom?.Id) return;
            SelectedRoom = null;
            Status = "Вы больше не в этом канале";
        });

        _connection.ChannelOverridesReceived += r => OnUi(() =>
        {
            ChannelOverrides.Clear();
            foreach (var o in r.Overrides) ChannelOverrides.Add(o);
        });

        // ServerSessionViewModel не знает, какой канал сейчас выбран во вкладке
        // "Каналы" (это состояние UI, ему тут не место) — просто сообщает наружу,
        // что оверрайды поменялись; ServerSettingsViewModel сам решает, перезапросить
        // ли их заново для канала, который сейчас открыт в редакторе.
        _connection.SetChannelOverrideResponseReceived += r => OnUi(() =>
        {
            Status = r.IsSuccess ? "Права канала обновлены" : "Не удалось обновить права канала";
            if (r.IsSuccess) ChannelOverrideChanged?.Invoke();
        });

        _connection.DeleteChannelOverrideResponseReceived += r => OnUi(() =>
        {
            Status = r.IsSuccess ? "Оверрайд сброшен" : "Не удалось сбросить оверрайд";
            if (r.IsSuccess) ChannelOverrideChanged?.Invoke();
        });

        _connection.ChannelKickResponseReceived += r => OnUi(() =>
            Status = r.IsSuccess ? "Пользователь кикнут из канала" : "Не удалось кикнуть из канала");
        _connection.ChannelUnbanResponseReceived += r => OnUi(() =>
            Status = r.IsSuccess ? "Пользователь разбанен в канале" : "Не удалось разбанить в канале");
        _connection.ChannelMuteResponseReceived += r => OnUi(() =>
            Status = r.IsSuccess ? "Пользователь заглушен в канале" : "Не удалось заглушить в канале");
        _connection.ChannelUnmuteResponseReceived += r => OnUi(() =>
            Status = r.IsSuccess ? "Пользователь размьючен в канале" : "Не удалось размьютить в канале");

        // Точечная синхронизация, а не Clear()+пересборка — ListBox.SelectedItem у списка
        // комнат привязан двусторонне к SelectedRoom, и полная очистка коллекции сбрасывает
        // выделение в null ещё до того, как список наполнится заново (Avalonia сама снимает
        // выделение, когда ItemsSource становится пустым, а обратно оно не восстанавливается
        // автоматически). Раньше это проявлялось как потеря открытой комнаты при любом
        // обновлении списка, пока в нём была выбрана комната — не только после реконнекта,
        // но и, например, при создании новой комнаты самим пользователем.
        _connection.RoomListReceived += r => OnUi(() => SyncRooms(r.Rooms));

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
            // Запрос истории уходит асинхронно (см. SwitchToRoomAsync) — если пользователь
            // успел выбрать другую комнату (или её вовсе не выбрал) до того, как ответ
            // вернулся, применять его уже нельзя: это история чужой комнаты.
            if (r.Messages.Count > 0 && r.Messages[0].RoomId != SelectedRoom?.Id) return;
            Messages.Clear();
            foreach (var m in r.Messages)
                Messages.Add(CreateMessageItem(m.SenderId, m.SenderName, m.Text, m.Timestamp));
        });

        _connection.MessageReceived += m => OnUi(() =>
        {
            if (m.RoomId != SelectedRoom?.Id) return;
            Messages.Add(CreateMessageItem(m.SenderId, m.SenderName, m.Text, m.Timestamp));
            // Сообщение — верный признак того, что печатать перестали, а новый TypingBroadcast
            // от этого отправителя, если он продолжит, придёт заново.
            if (_typingUsers.Remove(m.SenderId)) RefreshTypingIndicator();
        });

        _connection.Disconnected += ex => OnUi(() =>
        {
            IsLoggedIn = false;
            if (_userInitiatedDisconnect)
            {
                Status = "Отключено";
                return;
            }
            Status = $"Соединение разорвано: {ex?.Message ?? "connection closed"}";
            StartReconnectLoop();
        });
    }

    // Нарастающая пауза между попытками (2с → 30с), пока соединение не восстановится
    // или пользователь не отменит его сам (ручной "Log in" или удаление сервера из рейла).
    // Отменяет предыдущий цикл, если он ещё жив, и всегда запускает новый с нуля —
    // безопасно вызывать повторно (в т.ч. из catch у ручного Login, если сама ручная
    // попытка тоже не удалась: раньше цикл в этом случае молча умирал навсегда,
    // ничего не перезапуская, и пользователь оставался с текстом ошибки без дальнейших
    // автоматических попыток).
    private void StartReconnectLoop()
    {
        _reconnectCts?.Cancel();
        var cts = new CancellationTokenSource();
        _reconnectCts = cts;
        _ = ReconnectLoopAsync(cts.Token);
    }

    private async Task ReconnectLoopAsync(CancellationToken ct)
    {
        var delay = TimeSpan.FromSeconds(2);
        var maxDelay = TimeSpan.FromSeconds(30);
        try
        {
            while (!ct.IsCancellationRequested)
            {
                OnUi(() => Status = $"Переподключение через {(int)delay.TotalSeconds} сек...");
                await Task.Delay(delay, ct);

                OnUi(() => Status = "Переподключение...");
                try
                {
                    await _connection.ConnectAsync(Host, Port, ct);
                    await _connection.LoginAsync(Username, Password);
                    return; // соединение восстановлено; итог логина обработает AuthResponseReceived
                }
                catch (ServerCertificateChangedException ex)
                {
                    // Отпечаток сервера изменился — может значить подмену, поэтому не долбим
                    // дальше автоматически, тут нужно осознанное решение пользователя.
                    OnUi(() => Status = $"Сертификат сервера изменился: {ex.Message}");
                    return;
                }
                catch (Exception ex) when (ex is not OperationCanceledException)
                {
                    OnUi(() => Status = $"Не удалось переподключиться: {ex.Message}");
                    delay = TimeSpan.FromSeconds(Math.Min(delay.TotalSeconds * 2, maxDelay.TotalSeconds));
                }
            }
        }
        catch (OperationCanceledException) { /* отменено явно — ручной Log in или удаление сервера */ }
    }

    private void PersistIfNeeded()
    {
        if (IsPersisted) return;
        IsPersisted = true;
        OnPropertyChanged(nameof(IsPersisted));
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
    // Пользователь с несколькими ролями показывается только под ОДНОЙ — той, что
    // даёт больше прав (см. RoleRank) — иначе он дублировался бы в каждой группе,
    // которой принадлежит хоть одна его роль.
    private void RebuildMemberGroups()
    {
        MemberGroups.Clear();
        AllMembers.Clear();
        if (_rolesRaw.Count == 0 || _usersRaw.Count == 0) return;

        var groupsByRoleId = new Dictionary<long, RoleGroupViewModel>();
        var withoutRole = new List<UserInfo>();

        foreach (var user in _usersRaw)
        {
            var primary = PrimaryRole(user);
            if (primary is null) { withoutRole.Add(user); continue; }

            if (!groupsByRoleId.TryGetValue(primary.Id, out var group))
            {
                group = new RoleGroupViewModel(primary.DisplayName);
                groupsByRoleId[primary.Id] = group;
            }
            var member = CreateMemberViewModel(user);
            group.Members.Add(member);
            AllMembers.Add(member);
        }

        // Порядок групп — как в _rolesRaw, чтобы панель не перетасовывалась при каждом обновлении.
        foreach (var role in _rolesRaw)
        {
            if (groupsByRoleId.TryGetValue(role.Id, out var group)) MemberGroups.Add(group);
        }

        // На практике не бывает — CreateUser всегда назначает admin либо guest —
        // но не полагаемся на это молча, если данные когда-нибудь разойдутся.
        if (withoutRole.Count > 0)
        {
            var group = new RoleGroupViewModel("Без роли");
            foreach (var user in withoutRole)
            {
                var member = CreateMemberViewModel(user);
                group.Members.Add(member);
                AllMembers.Add(member);
            }
            MemberGroups.Add(group);
        }
    }

    // Роль с наибольшим количеством прав среди тех, что есть у пользователя.
    // admin и owner хранят permissions=0 (см. Permissions.h на сервере — их сила не
    // в битовой маске), поэтому ранжируются отдельно, а не через popcount как остальные.
    private RoleInfo? PrimaryRole(UserInfo user)
    {
        RoleInfo? best = null;
        int bestRank = -1;
        foreach (var role in _rolesRaw)
        {
            if (!user.RoleIds.Contains(role.Id)) continue;
            int rank = RoleRank(role);
            if (rank > bestRank) { bestRank = rank; best = role; }
        }
        return best;
    }

    private static int RoleRank(RoleInfo role) => role.Id switch
    {
        RoleIds.Owner => int.MaxValue,
        RoleIds.Admin => int.MaxValue - 1,
        _ => System.Numerics.BitOperations.PopCount(role.Permissions)
    };

    private MemberViewModel CreateMemberViewModel(UserInfo user)
    {
        var member = new MemberViewModel(
            user.Id, user.Username, user.RoleIds, user.Online, user.Ip, _rolesRaw, _nicknames.Get(Host, Port, user.Id),
            isSelf: user.Id == CurrentUserId,
            canManageRoles: CanManageRoles,
            canManageServerBans: CanManageServerBans,
            canManageUsers: CanManageUsers,
            block: BlockUser);
        ApplyOrFetchAvatar(member, user.AvatarVersion);
        return member;
    }

    private string AvatarCacheKey(long userId) => $"{Host}:{Port}:user:{userId}";

    // version=0 — аватарки нет. Иначе сперва проверяем дисковый кэш (не тратим сеть,
    // если версия не изменилась с прошлого раза) и только при промахе запрашиваем байты.
    private void ApplyOrFetchAvatar(MemberViewModel member, long version)
    {
        if (version == 0) { member.Avatar = null; return; }
        var cached = _avatars.TryLoad(AvatarCacheKey(member.UserId), version);
        if (cached is not null) { member.Avatar = cached; return; }
        _ = _connection.FetchAvatarAsync(member.UserId);
    }

    // Локальный никнейм подставляется и здесь же — иначе история чата пока не
    // переименована показывала бы серверный логин, что и было репортнуто как баг.
    private MessageItem CreateMessageItem(long senderId, string senderName, string text, long timestamp) =>
        new(senderId, senderName, text, FormatTime(timestamp)) { LocalNickname = _nicknames.Get(Host, Port, senderId) };

    [RelayCommand]
    private void RenameMember(MemberViewModel member) => MemberRenameRequested?.Invoke(member);

    [RelayCommand]
    private void ToggleMembers() => ShowMembers = !ShowMembers;

    [RelayCommand]
    private void OpenServerSettings() => ServerSettingsRequested?.Invoke(this);

    private void AddRoom(RoomInfo room)
    {
        Rooms.Add(room);
        (room.Type == RoomType.Voice ? VoiceRooms : TextRooms).Add(room);
    }

    // Убирает комнаты, которых больше нет на сервере, добавляет появившиеся новые и
    // обновляет на месте те, что изменились (важно для canSendMessages — оно пересчитывается
    // заново на каждый запрос списка, и без этой ветки уже добавленная комната так и осталась
    // бы с устаревшим значением до переподключения) — не трогая остальные, чтобы не терять
    // текущее выделение в списке (см. RoomListReceived).
    private void SyncRooms(IReadOnlyList<RoomInfo> freshRooms)
    {
        for (int i = Rooms.Count - 1; i >= 0; i--)
        {
            var existing = Rooms[i];
            if (freshRooms.Any(room => room.Id == existing.Id)) continue;
            Rooms.RemoveAt(i);
            TextRooms.Remove(existing);
            VoiceRooms.Remove(existing);
        }

        foreach (var room in freshRooms)
        {
            var existing = Rooms.FirstOrDefault(r => r.Id == room.Id);
            if (existing is null) AddRoom(room);
            else if (existing != room) ReplaceRoom(existing, room);
        }
    }

    // Обновляет имя комнаты на месте (Replace, не Remove+Add) во всех трёх коллекциях.
    // SelectedRoom правится в обход сеттера — иначе OnSelectedRoomChanged воспринял бы
    // переименование как смену комнаты и заново дёрнул бы историю/подписку на неё.
    private void ReplaceRoom(RoomInfo oldRoom, RoomInfo newRoom)
    {
        int i = Rooms.IndexOf(oldRoom);
        if (i >= 0) Rooms[i] = newRoom;
        var group = oldRoom.Type == RoomType.Voice ? VoiceRooms : TextRooms;
        int j = group.IndexOf(oldRoom);
        if (j >= 0) group[j] = newRoom;

        if (SelectedRoom?.Id == newRoom.Id)
        {
#pragma warning disable MVVMTK0034 // намеренно в обход генерируемого сеттера — см. комментарий выше
            _selectedRoom = newRoom;
#pragma warning restore MVVMTK0034
            OnPropertyChanged(nameof(SelectedRoom));
        }
    }

    // Пустая строка сбрасывает локальный никнейм обратно к настоящему имени. Обновляет
    // и уже отрисованную ленту сообщений этого отправителя — раньше никнейм подхватывался
    // только у новых сообщений, а старые в истории так и оставались с логином.
    public void SetLocalNickname(long userId, string nickname)
    {
        string? stored = string.IsNullOrEmpty(nickname) ? null : nickname;
        _nicknames.Set(Host, Port, userId, stored);

        var member = MemberGroups.SelectMany(g => g.Members).FirstOrDefault(m => m.UserId == userId);
        if (member is not null) member.LocalNickname = stored;

        foreach (var message in Messages.Where(m => m.SenderId == userId))
            message.LocalNickname = stored;
    }

    public Task AssignRoleAsync(long userId, long roleId) => _connection.AssignRoleAsync(userId, roleId);
    public Task RemoveRoleAsync(long userId, long roleId) => _connection.RemoveRoleAsync(userId, roleId);
    public Task CreateRoleAsync(string name, uint permissions, string displayName) =>
        _connection.CreateRoleAsync(name, permissions, displayName);
    public Task UpdateRoleAsync(long roleId, string name, uint permissions, string displayName) =>
        _connection.UpdateRoleAsync(roleId, name, permissions, displayName);
    public Task DeleteRoleAsync(long roleId) => _connection.DeleteRoleAsync(roleId);
    public Task DeleteUserAsync(long userId) => _connection.DeleteUserAsync(userId);
    public Task UploadAvatarAsync(byte[] data) => _connection.UploadAvatarAsync(data);
    public Task UploadServerIconAsync(byte[] data) => _connection.UploadServerIconAsync(data);
    public Task SetServerInfoAsync(string name, string description) => _connection.SetServerInfoAsync(name, description);
    public Task BanUserSessionAsync(long userId) => _connection.BanUserSessionAsync(userId);
    public Task RefreshUsersAsync() => _connection.ListUsersAsync();
    public Task ListBannedIpsAsync() => _connection.ListBannedIpsAsync();
    public Task BanIpAsync(string ip) => _connection.BanIpAsync(ip);
    public Task UnbanIpAsync(string ip) => _connection.UnbanIpAsync(ip);

    // Отдельное имя от команды "CreateRoomAsync" — та уже занята полем ввода в
    // обычной боковой панели (создаёт только текстовый канал), эта — вкладкой
    // "Каналы" с явным выбором типа.
    public Task CreateChannelAsync(string name, RoomType type) => _connection.CreateRoomAsync(name, type);
    public Task UpdateRoomAsync(long roomId, string name) => _connection.UpdateRoomAsync(roomId, name);
    public Task DeleteRoomAsync(long roomId) => _connection.DeleteRoomAsync(roomId);
    public Task GetChannelOverridesAsync(long roomId) => _connection.GetChannelOverridesAsync(roomId);
    public Task SetChannelOverrideAsync(long roomId, long roleId, uint allow, uint deny) =>
        _connection.SetChannelOverrideAsync(roomId, roleId, allow, deny);
    public Task DeleteChannelOverrideAsync(long roomId, long roleId) => _connection.DeleteChannelOverrideAsync(roomId, roleId);
    public Task KickFromChannelAsync(long roomId, long userId) => _connection.KickFromChannelAsync(roomId, userId);
    public Task UnbanFromChannelAsync(long roomId, long userId) => _connection.UnbanFromChannelAsync(roomId, userId);
    public Task MuteInChannelAsync(long roomId, long userId) => _connection.MuteInChannelAsync(roomId, userId);
    public Task UnmuteInChannelAsync(long roomId, long userId) => _connection.UnmuteInChannelAsync(roomId, userId);

    private void BlockUser(long userId) => _ = _connection.BanUserSessionAsync(userId);

    // Гасит просроченные записи "печатает" и пересобирает строку для отображения.
    // Планируется заново на каждый входящий TypingBroadcast (см. конструктор) —
    // отдельного постоянного таймера не заводим, обычного Task.Delay достаточно.
    private void RefreshTypingIndicator()
    {
        var now = DateTime.UtcNow;
        foreach (var expiredId in _typingUsers.Where(kv => kv.Value.Expiry <= now).Select(kv => kv.Key).ToList())
            _typingUsers.Remove(expiredId);

        TypingIndicatorText = _typingUsers.Count switch
        {
            0 => "",
            1 => $"{_typingUsers.Values.First().Name} печатает...",
            _ => $"{string.Join(", ", _typingUsers.Values.Select(v => v.Name))} печатают..."
        };
    }

    // Троттлинг: шлём "я печатаю" не чаще раза в 3 секунды, а не на каждую нажатую клавишу.
    partial void OnMessageTextChanged(string value)
    {
        if (string.IsNullOrEmpty(value) || SelectedRoom is null) return;
        var now = DateTime.UtcNow;
        if (now - _lastTypingSentUtc < TimeSpan.FromSeconds(3)) return;
        _lastTypingSentUtc = now;
        _ = _connection.TypingAsync(SelectedRoom.Id);
    }

    // Вызывается MainWindowViewModel при удалении сервера из рейла —
    // закрывает соединение, чтобы фоновый поток приёма не остался висеть.
    public async Task DisconnectAsync()
    {
        _userInitiatedDisconnect = true;
        _reconnectCts?.Cancel();
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
        // Иконка и имя сервера видны в рейле уже на экране логина, не только после
        // входа — единственные запросы, которые gateway обрабатывает и без сессии.
        _ = _connection.FetchServerIconAsync();
        _ = _connection.FetchServerInfoAsync();
    }

    // Тот же "Log in" в UI служит и ручным переподключением: если фоновый цикл сейчас
    // ждёт паузу между попытками, клик должен прервать её и попробовать сразу же, а не
    // запускать второе параллельное подключение поверх того же _connection.
    [RelayCommand]
    private async Task LoginAsync()
    {
        if (!_guardConnect(this)) return;
        _reconnectCts?.Cancel();
        try
        {
            await EnsureConnectedAsync();
            if (!_guardFingerprint(this)) return;
            await _connection.LoginAsync(Username, Password);
        }
        catch (Exception ex)
        {
            Status = $"Error: {ex.Message}";
            // Ручная попытка не удалась (сервер всё ещё недоступен) — не бросаем
            // автоматику мёртвой, отдаём попытки обратно фоновому циклу.
            StartReconnectLoop();
        }
    }

    [RelayCommand]
    private async Task RegisterAsync()
    {
        if (!_guardConnect(this)) return;
        _reconnectCts?.Cancel();
        try
        {
            await EnsureConnectedAsync();
            if (!_guardFingerprint(this)) return;
            await _connection.RegisterAsync(Username, Password);
        }
        catch (Exception ex)
        {
            Status = $"Error: {ex.Message}";
            StartReconnectLoop();
        }
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
        // Кнопка отключена по этому же условию (см. CanSendInSelectedRoom), но Enter
        // в TextBox идёт через KeyBinding и не смотрит на IsEnabled кнопки.
        if (!CanSendInSelectedRoom) return;
        await _connection.SendTextAsync(SelectedRoom.Id, MessageText);
        MessageText = "";
    }

    // Срабатывает при клике по комнате в списке
    partial void OnSelectedRoomChanged(RoomInfo? value)
    {
        Messages.Clear();
        _typingUsers.Clear();
        TypingIndicatorText = "";

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

// Одно сообщение в ленте. LocalNickname — чисто клиентская подмена отображаемого
// имени отправителя (см. LocalNicknameStore); Author пересчитывается реактивно,
// поэтому переименование участника задним числом обновляет уже отрисованную историю.
public sealed partial class MessageItem : ObservableObject
{
    public long SenderId { get; }
    public string SenderName { get; }
    public string Text { get; }
    public string Time { get; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(Author))]
    private string? _localNickname;

    public string Author => string.IsNullOrEmpty(LocalNickname) ? SenderName : LocalNickname;

    public MessageItem(long senderId, string senderName, string text, string time)
    {
        SenderId = senderId;
        SenderName = senderName;
        Text = text;
        Time = time;
    }
}
