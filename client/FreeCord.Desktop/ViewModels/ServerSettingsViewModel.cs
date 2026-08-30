using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using FreeCord.Protocol;

namespace FreeCord.Desktop.ViewModels;

/// <summary>
/// Окно настроек сервера — самодельная горизонтальная полоса вкладок вместо
/// стандартного Avalonia TabControl (его светлая тема-по-умолчанию не сочеталась
/// с тёмной палитрой всего остального приложения). SelectedTabIndex — единственный
/// источник правды, остальное (видимость панели, цвет активной вкладки) — вычисляемые
/// свойства от него; тот же приём, что уже проверен на PresenceBrush в MemberViewModel,
/// а не привязка Classes к биту напрямую из XAML — тот способ уже подводил на практике.
/// </summary>
public partial class ServerSettingsViewModel : ViewModelBase
{
    private static readonly IBrush ActiveBrush = Brushes.White;
    private static readonly IBrush MutedBrush = new SolidColorBrush(Color.Parse("#949BA4"));
    private static readonly IBrush AccentBrush = new SolidColorBrush(Color.Parse("#5865F2"));
    private static readonly IBrush TransparentBrush = Brushes.Transparent;

    public ServerSessionViewModel Server { get; }

    // Панель — UserControl внутри оверлея, а не своё Window, поэтому не может достать
    // MainWindowViewModel (которому принадлежит ActiveServerSettings) через $parent[Window] —
    // тот путь не резолвится статически из чужого файла с компилируемыми биндингами.
    // Проще передать готовый колбэк закрытия при создании.
    private readonly Action _onClose;

    [ObservableProperty] private string _nameDraft;
    [ObservableProperty] private string _descriptionDraft;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsInfoTab))]
    [NotifyPropertyChangedFor(nameof(IsRolesTab))]
    [NotifyPropertyChangedFor(nameof(IsUsersTab))]
    [NotifyPropertyChangedFor(nameof(IsChannelsTab))]
    [NotifyPropertyChangedFor(nameof(IsMessagesTab))]
    [NotifyPropertyChangedFor(nameof(IsFilesTab))]
    [NotifyPropertyChangedFor(nameof(IsVoiceTab))]
    [NotifyPropertyChangedFor(nameof(IsVideoTab))]
    [NotifyPropertyChangedFor(nameof(InfoTabColor))]
    [NotifyPropertyChangedFor(nameof(RolesTabColor))]
    [NotifyPropertyChangedFor(nameof(UsersTabColor))]
    [NotifyPropertyChangedFor(nameof(ChannelsTabColor))]
    [NotifyPropertyChangedFor(nameof(MessagesTabColor))]
    [NotifyPropertyChangedFor(nameof(FilesTabColor))]
    [NotifyPropertyChangedFor(nameof(VoiceTabColor))]
    [NotifyPropertyChangedFor(nameof(VideoTabColor))]
    [NotifyPropertyChangedFor(nameof(InfoTabIndicator))]
    [NotifyPropertyChangedFor(nameof(RolesTabIndicator))]
    [NotifyPropertyChangedFor(nameof(UsersTabIndicator))]
    [NotifyPropertyChangedFor(nameof(ChannelsTabIndicator))]
    [NotifyPropertyChangedFor(nameof(MessagesTabIndicator))]
    [NotifyPropertyChangedFor(nameof(FilesTabIndicator))]
    [NotifyPropertyChangedFor(nameof(VoiceTabIndicator))]
    [NotifyPropertyChangedFor(nameof(VideoTabIndicator))]
    private int _selectedTabIndex;

    public bool IsInfoTab => SelectedTabIndex == 0;
    public bool IsRolesTab => SelectedTabIndex == 1;
    public bool IsUsersTab => SelectedTabIndex == 2;
    public bool IsChannelsTab => SelectedTabIndex == 3;
    public bool IsMessagesTab => SelectedTabIndex == 4;
    public bool IsFilesTab => SelectedTabIndex == 5;
    public bool IsVoiceTab => SelectedTabIndex == 6;
    public bool IsVideoTab => SelectedTabIndex == 7;

    // Цвет текста активной вкладки (белый) против неактивной (приглушённый серый).
    public IBrush InfoTabColor => IsInfoTab ? ActiveBrush : MutedBrush;
    public IBrush RolesTabColor => IsRolesTab ? ActiveBrush : MutedBrush;
    public IBrush UsersTabColor => IsUsersTab ? ActiveBrush : MutedBrush;
    public IBrush ChannelsTabColor => IsChannelsTab ? ActiveBrush : MutedBrush;
    public IBrush MessagesTabColor => IsMessagesTab ? ActiveBrush : MutedBrush;
    public IBrush FilesTabColor => IsFilesTab ? ActiveBrush : MutedBrush;
    public IBrush VoiceTabColor => IsVoiceTab ? ActiveBrush : MutedBrush;
    public IBrush VideoTabColor => IsVideoTab ? ActiveBrush : MutedBrush;

    // Акцентная полоска снизу активной вкладки — прозрачная у остальных.
    public IBrush InfoTabIndicator => IsInfoTab ? AccentBrush : TransparentBrush;
    public IBrush RolesTabIndicator => IsRolesTab ? AccentBrush : TransparentBrush;
    public IBrush UsersTabIndicator => IsUsersTab ? AccentBrush : TransparentBrush;
    public IBrush ChannelsTabIndicator => IsChannelsTab ? AccentBrush : TransparentBrush;
    public IBrush MessagesTabIndicator => IsMessagesTab ? AccentBrush : TransparentBrush;
    public IBrush FilesTabIndicator => IsFilesTab ? AccentBrush : TransparentBrush;
    public IBrush VoiceTabIndicator => IsVoiceTab ? AccentBrush : TransparentBrush;
    public IBrush VideoTabIndicator => IsVideoTab ? AccentBrush : TransparentBrush;

    // --- Вкладка "Роли" ---

    // Все биты прав в одном месте, с русской подписью — источник и для чекбоксов
    // редактора, и для их начального состояния при выборе роли.
    private static readonly (Permission Flag, string Label)[] AllPermissions =
    {
        (Permission.ViewChannel, "Видеть канал в списке"),
        (Permission.OpenChannel, "Открывать канал"),
        (Permission.SendMessages, "Отправлять сообщения"),
        (Permission.SendFiles, "Отправлять файлы (пока не используется — задел под обмен файлами)"),
        (Permission.ManageChannel, "Создавать и настраивать каналы"),
        (Permission.ManageRoles, "Управлять ролями"),
        (Permission.ManageServer, "Управлять настройками сервера"),
        (Permission.KickMembers, "Кикать и разбанивать участников канала"),
        (Permission.ManageChannelModeration, "Мьютить и размьючивать участников канала"),
        (Permission.ManageServerBans, "Банить по IP на уровне всего сервера"),
        (Permission.ManageUsers, "Удалять аккаунты пользователей"),
        (Permission.ManageMessages, "Настраивать параметры сообщений (пока не используется — задел под лимиты/скорость)"),
        (Permission.ManageFiles, "Настраивать обмен файлами (пока не используется — задел под лимиты хранилища)"),
    };

    // Owner скрыт из этого списка (и из выпадающих "Выдать роль"/"Убрать роль" в
    // "Пользователях", которые его переиспользуют) — сугубо системная роль-метка
    // неприкосновенности, её никто не должен ни видеть, ни редактировать через UI.
    // Server.Roles (полный список) при этом не трогаем — по нему считается группа
    // "Владелец" в обычной панели участников чата, ломать это не нужно.
    public IEnumerable<RoleInfo> Roles => Server.Roles.Where(r => r.Id != RoleIds.Owner);
    public ObservableCollection<PermissionOptionViewModel> PermissionOptions { get; } = new();

    [ObservableProperty] private string _newRoleName = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelectedRole))]
    [NotifyPropertyChangedFor(nameof(IsSelectedRoleAdmin))]
    [NotifyPropertyChangedFor(nameof(CanEditSelectedRoleName))]
    [NotifyPropertyChangedFor(nameof(CanDeleteSelectedRole))]
    private RoleInfo? _selectedRole;

    [ObservableProperty] private string _roleNameDraft = "";
    [ObservableProperty] private string _roleDisplayNameDraft = "";

    public bool HasSelectedRole => SelectedRole is not null;
    // admin — суперпользователь независимо от битов permissions (см. Permissions.h на
    // сервере), а UpdateRole для него всегда отказывает целиком, даже без смены имени —
    // поэтому редактор для него полностью read-only, а не просто "права ни на что не влияют".
    public bool IsSelectedRoleAdmin => SelectedRole?.Id == RoleIds.Admin;
    // Техническое имя (Name, не DisplayName) системной роли сервер менять не даёт —
    // см. UpdateRole в UserRepository.cpp. DisplayName и права системным ролям
    // (кроме admin) менять можно — сервер эту разницу тоже проверяет отдельно.
    public bool CanEditSelectedRoleName => SelectedRole is { IsSystem: false };
    public bool CanDeleteSelectedRole => SelectedRole is { IsSystem: false };

    public ServerSettingsViewModel(ServerSessionViewModel server, Action onClose)
    {
        Server = server;
        _onClose = onClose;
        _nameDraft = server.DisplayName;
        _descriptionDraft = server.Description;

        // Roles пересобирается заново (Clear + Add) при каждом RoleListReceived — после
        // своего же успешного изменения нужно перечитать SelectedRole из свежего списка,
        // иначе правая панель показывала бы старый снимок до следующего клика по роли.
        Server.Roles.CollectionChanged += OnRolesCollectionChanged;

        // Перестроить строки редактора оверрайдов при получении свежих оверрайдов для
        // выбранного канала (список ролей туда же подмешивает сам OnRolesCollectionChanged).
        Server.ChannelOverrides.CollectionChanged += OnChannelOverridesCollectionChanged;
        Server.ChannelOverrideChanged += OnChannelOverrideChanged;

        // Channels — тот же вычисляемый фильтр поверх Server.Rooms, что и Roles поверх
        // Server.Roles (см. выше): системный канал ("system") нельзя переименовать или
        // удалить (сервер отказывает — RoomUpdateResult/RoomDeleteResult::SystemRoom),
        // поэтому ему вообще нечего делать в редакторе каналов. В обычном списке комнат
        // в чате (Server.Rooms напрямую) он остаётся — там его по-прежнему видят и
        // открывают все, ограничена только отправка сообщений (EffectivePermissionsInRoom).
        Server.Rooms.CollectionChanged += OnRoomsCollectionChanged;

        // Открывшему панель гарантированно доступна хотя бы одна вкладка (иначе кнопка
        // настроек была бы не видна вовсе, см. CanOpenServerSettings) — но не обязательно
        // самая первая: показываем ту, на которую реально хватает прав.
        _selectedTabIndex = FirstAccessibleTabIndex();
    }

    private int FirstAccessibleTabIndex()
    {
        if (Server.CanManageServer) return 0;
        if (Server.CanManageRoles) return 1;
        if (Server.CanAccessUsersTab) return 2;
        if (Server.CanManageChannel) return 3;
        if (Server.CanManageMessages) return 4;
        if (Server.CanManageFiles) return 5;
        return 0; // недостижимо на практике — сама кнопка открытия панели уже требует хоть одно из прав выше
    }

    private void OnRolesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        // Roles — вычисляемый фильтр поверх Server.Roles, а не отдельная ObservableCollection,
        // поэтому об его изменении нужно уведомить явно.
        OnPropertyChanged(nameof(Roles));
        if (SelectedRole is not null) SelectedRole = Roles.FirstOrDefault(r => r.Id == SelectedRole.Id);
        // Список ролей — те же строки, что и в редакторе оверрайдов канала (за вычетом
        // owner) — новая/удалённая роль должна сразу отразиться в обоих местах.
        RebuildChannelOverrideRows();
    }

    private void OnChannelOverridesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e) =>
        RebuildChannelOverrideRows();

    partial void OnSelectedRoleChanged(RoleInfo? value)
    {
        RoleNameDraft = value?.Name ?? "";
        RoleDisplayNameDraft = value?.DisplayName ?? "";

        PermissionOptions.Clear();
        if (value is null) return;

        // admin хранит permissions=0 в БД (его сила — сентинел на сервере, не битовая
        // маска, см. Permissions.h), но по факту имеет все права — показываем все
        // галочки включёнными, а не тем, что реально лежит в столбце, иначе выглядело
        // бы так, будто у admin нет никаких прав вовсе. Редактирование всё равно
        // недоступно (canEdit=false), так что расхождение с БД никогда не уйдёт на сервер.
        bool isAdmin = IsSelectedRoleAdmin;
        foreach (var (flag, label) in AllPermissions)
        {
            bool isChecked = isAdmin || (value.Permissions & (uint)flag) != 0;
            PermissionOptions.Add(new PermissionOptionViewModel(flag, label, isChecked, isEnabled: !isAdmin));
        }
    }

    [RelayCommand]
    private void SelectRole(RoleInfo role) => SelectedRole = role;

    [RelayCommand]
    private Task CreateRole()
    {
        if (string.IsNullOrWhiteSpace(NewRoleName)) return Task.CompletedTask;
        var name = NewRoleName.Trim();
        NewRoleName = "";
        return Server.CreateRoleAsync(name, 0, "");
    }

    [RelayCommand]
    private Task SaveRole()
    {
        if (SelectedRole is null || IsSelectedRoleAdmin) return Task.CompletedTask;
        string name = CanEditSelectedRoleName ? RoleNameDraft.Trim() : SelectedRole.Name;
        uint permissions = PermissionOptions.Where(p => p.IsChecked)
            .Aggregate(0u, (mask, p) => mask | (uint)p.Flag);
        return Server.UpdateRoleAsync(SelectedRole.Id, name, permissions, RoleDisplayNameDraft.Trim());
    }

    [RelayCommand]
    private Task DeleteRole()
    {
        if (SelectedRole is null || !CanDeleteSelectedRole) return Task.CompletedTask;
        var id = SelectedRole.Id;
        SelectedRole = null;
        return Server.DeleteRoleAsync(id);
    }

    [RelayCommand]
    private void Close()
    {
        Server.Roles.CollectionChanged -= OnRolesCollectionChanged;
        Server.ChannelOverrides.CollectionChanged -= OnChannelOverridesCollectionChanged;
        Server.ChannelOverrideChanged -= OnChannelOverrideChanged;
        Server.Rooms.CollectionChanged -= OnRoomsCollectionChanged;
        _onClose();
    }

    [RelayCommand] private void SelectInfoTab() => SelectedTabIndex = 0;
    [RelayCommand] private void SelectRolesTab() => SelectedTabIndex = 1;

    [RelayCommand]
    private Task SelectUsersTab()
    {
        SelectedTabIndex = 2;
        // Список участников уже актуален (обновляется пушем), а бан-лист по IP —
        // нет: его никто не запрашивает, пока не открыта именно эта вкладка.
        return Server.ListBannedIpsAsync();
    }

    [RelayCommand] private void SelectChannelsTab() => SelectedTabIndex = 3;
    [RelayCommand] private void SelectMessagesTab() => SelectedTabIndex = 4;
    [RelayCommand] private void SelectFilesTab() => SelectedTabIndex = 5;
    [RelayCommand] private void SelectVoiceTab() => SelectedTabIndex = 6;
    [RelayCommand] private void SelectVideoTab() => SelectedTabIndex = 7;

    [RelayCommand]
    private Task SaveInfo() => Server.SetServerInfoAsync(NameDraft, DescriptionDraft);

    // --- Вкладка "Пользователи" ---

    public ObservableCollection<MemberViewModel> Users => Server.AllMembers;
    public ObservableCollection<string> BannedIps => Server.BannedIps;

    [ObservableProperty] private string _newBanIp = "";

    [RelayCommand]
    private Task BanIp()
    {
        if (string.IsNullOrWhiteSpace(NewBanIp)) return Task.CompletedTask;
        var ip = NewBanIp.Trim();
        NewBanIp = "";
        return Server.BanIpAsync(ip);
    }

    [RelayCommand]
    private Task UnbanIp(string ip) => Server.UnbanIpAsync(ip);

    // --- Вкладка "Каналы" ---

    // Системный канал сюда не попадает — им нельзя управлять (см. комментарий у подписки
    // на Server.Rooms.CollectionChanged в конструкторе).
    public IEnumerable<RoomInfo> Channels => Server.Rooms.Where(r => r.Id != RoomIds.System);

    private void OnRoomsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        OnPropertyChanged(nameof(Channels));
        // Если выбранный канал вдруг пропал из списка (удалили из другого места) —
        // сбрасываем выделение, как и при обычном DeleteChannel.
        if (SelectedChannel is not null && Server.Rooms.All(r => r.Id != SelectedChannel.Id)) SelectedChannel = null;
    }
    public ObservableCollection<ChannelOverrideRowViewModel> ChannelOverrideRows { get; } = new();

    [ObservableProperty] private string _newChannelName = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsNewChannelTypeText))]
    [NotifyPropertyChangedFor(nameof(IsNewChannelTypeVoice))]
    private RoomType _newChannelType = RoomType.Text;

    // Радиокнопки вместо ComboBox с enum — проще и без риска: два bool-свойства,
    // завязанные на один и тот же RoomType, тот же принцип, что и везде в этом файле.
    public bool IsNewChannelTypeText
    {
        get => NewChannelType == RoomType.Text;
        set { if (value) NewChannelType = RoomType.Text; }
    }
    public bool IsNewChannelTypeVoice
    {
        get => NewChannelType == RoomType.Voice;
        set { if (value) NewChannelType = RoomType.Voice; }
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelectedChannel))]
    private RoomInfo? _selectedChannel;

    [ObservableProperty] private string _channelNameDraft = "";

    // Модерация — выбор "кого" отдельно от выбора канала, обе кнопки требуют обоих.
    [ObservableProperty] private MemberViewModel? _moderationTarget;

    public bool HasSelectedChannel => SelectedChannel is not null;

    [RelayCommand]
    private void SelectChannel(RoomInfo room) => SelectedChannel = room;

    partial void OnSelectedChannelChanged(RoomInfo? value)
    {
        ChannelNameDraft = value?.Name ?? "";
        ModerationTarget = null;
        if (value is not null) _ = Server.GetChannelOverridesAsync(value.Id);
        else Server.ChannelOverrides.Clear(); // сам вызовет RebuildChannelOverrideRows через подписку
    }

    private void OnChannelOverrideChanged()
    {
        // ServerSessionViewModel не знает, какой канал сейчас открыт в редакторе —
        // перезапрашиваем сами, раз что-то из оверрайдов только что успешно поменялось.
        if (SelectedChannel is not null) _ = Server.GetChannelOverridesAsync(SelectedChannel.Id);
    }

    private void RebuildChannelOverrideRows()
    {
        ChannelOverrideRows.Clear();
        if (SelectedChannel is null) return;
        foreach (var role in Roles) // уже без owner, тот же список, что и на вкладке "Роли"
        {
            var existing = Server.ChannelOverrides.FirstOrDefault(o => o.RoleId == role.Id);
            uint allow = existing?.Allow ?? 0;
            uint deny = existing?.Deny ?? 0;
            ChannelOverrideRows.Add(new ChannelOverrideRowViewModel(this, role.Id, role.DisplayName,
                StateFor(allow, deny, Permission.ViewChannel),
                StateFor(allow, deny, Permission.OpenChannel),
                StateFor(allow, deny, Permission.SendMessages)));
        }
    }

    private static OverrideState StateFor(uint allow, uint deny, Permission flag)
    {
        uint bit = (uint)flag;
        if ((allow & bit) != 0) return OverrideState.Allow;
        if ((deny & bit) != 0) return OverrideState.Deny;
        return OverrideState.Inherit;
    }

    // Вызывается строкой редактора при любом изменении одного из трёх её тумблеров —
    // пересчитывает итоговую маску по всем трём сразу (не только по тому, что поменялся)
    // и либо сохраняет оверрайд, либо удаляет его целиком, если всё вернулось к "Наследовать".
    internal void ApplyChannelOverride(ChannelOverrideRowViewModel row)
    {
        if (SelectedChannel is null) return;
        uint allow = 0, deny = 0;
        Accumulate(ref allow, ref deny, Permission.ViewChannel, row.ViewChannel);
        Accumulate(ref allow, ref deny, Permission.OpenChannel, row.OpenChannel);
        Accumulate(ref allow, ref deny, Permission.SendMessages, row.SendMessages);

        _ = allow == 0 && deny == 0
            ? Server.DeleteChannelOverrideAsync(SelectedChannel.Id, row.RoleId)
            : Server.SetChannelOverrideAsync(SelectedChannel.Id, row.RoleId, allow, deny);
    }

    private static void Accumulate(ref uint allow, ref uint deny, Permission flag, OverrideState state)
    {
        uint bit = (uint)flag;
        if (state == OverrideState.Allow) allow |= bit;
        else if (state == OverrideState.Deny) deny |= bit;
    }

    [RelayCommand]
    private Task CreateChannel()
    {
        if (string.IsNullOrWhiteSpace(NewChannelName)) return Task.CompletedTask;
        var name = NewChannelName.Trim();
        NewChannelName = "";
        return Server.CreateChannelAsync(name, NewChannelType);
    }

    [RelayCommand]
    private Task RenameChannel()
    {
        if (SelectedChannel is null || string.IsNullOrWhiteSpace(ChannelNameDraft)) return Task.CompletedTask;
        return Server.UpdateRoomAsync(SelectedChannel.Id, ChannelNameDraft.Trim());
    }

    [RelayCommand]
    private Task DeleteChannel()
    {
        if (SelectedChannel is null) return Task.CompletedTask;
        var id = SelectedChannel.Id;
        SelectedChannel = null;
        return Server.DeleteRoomAsync(id);
    }

    [RelayCommand]
    private Task ModerationKick() =>
        SelectedChannel is not null && ModerationTarget is not null
            ? Server.KickFromChannelAsync(SelectedChannel.Id, ModerationTarget.UserId)
            : Task.CompletedTask;

    [RelayCommand]
    private Task ModerationUnban() =>
        SelectedChannel is not null && ModerationTarget is not null
            ? Server.UnbanFromChannelAsync(SelectedChannel.Id, ModerationTarget.UserId)
            : Task.CompletedTask;

    [RelayCommand]
    private Task ModerationMute() =>
        SelectedChannel is not null && ModerationTarget is not null
            ? Server.MuteInChannelAsync(SelectedChannel.Id, ModerationTarget.UserId)
            : Task.CompletedTask;

    [RelayCommand]
    private Task ModerationUnmute() =>
        SelectedChannel is not null && ModerationTarget is not null
            ? Server.UnmuteInChannelAsync(SelectedChannel.Id, ModerationTarget.UserId)
            : Task.CompletedTask;
}

/// <summary>Тумблер одного права оверрайда: унаследовать от роли, явно разрешить
/// или явно запретить на этом конкретном канале.</summary>
public enum OverrideState { Inherit, Allow, Deny }

/// <summary>Одна строка редактора оверрайдов канала — одна роль, три права
/// (ViewChannel/OpenChannel/SendMessages — только они и учитываются per-канально,
/// см. CLAUDE.md). Любое изменение тумблера сразу уходит на сервер через владельца
/// (ApplyChannelOverride), отдельной кнопки "Сохранить" в этом редакторе нет.</summary>
public sealed partial class ChannelOverrideRowViewModel : ObservableObject
{
    private readonly ServerSettingsViewModel _owner;
    private readonly bool _isLoading;

    public long RoleId { get; }
    public string RoleDisplayName { get; }

    // Один и тот же набор трёх значений для всех трёх ComboBox строки — отображение
    // по-русски берёт на себя OverrideStateToLabelConverter, тут просто исходные значения.
    public static IReadOnlyList<OverrideState> AllStates { get; } =
        Enum.GetValues<OverrideState>();

    [ObservableProperty] private OverrideState _viewChannel;
    [ObservableProperty] private OverrideState _openChannel;
    [ObservableProperty] private OverrideState _sendMessages;

    public ChannelOverrideRowViewModel(ServerSettingsViewModel owner, long roleId, string roleDisplayName,
        OverrideState viewChannel, OverrideState openChannel, OverrideState sendMessages)
    {
        _owner = owner;
        _isLoading = true;
        RoleId = roleId;
        RoleDisplayName = roleDisplayName;
        _viewChannel = viewChannel;
        _openChannel = openChannel;
        _sendMessages = sendMessages;
        _isLoading = false;
    }

    partial void OnViewChannelChanged(OverrideState value) => Apply();
    partial void OnOpenChannelChanged(OverrideState value) => Apply();
    partial void OnSendMessagesChanged(OverrideState value) => Apply();

    // _isLoading защищает от повторной отправки на сервер того, что мы сами только
    // что с него и получили (конструктор выставляет три поля по очереди, каждое
    // через тот же генерируемый сеттер, что и обычное изменение пользователем).
    private void Apply()
    {
        if (_isLoading) return;
        _owner.ApplyChannelOverride(this);
    }
}

/// <summary>Один чекбокс в редакторе прав роли. IsEnabled фиксируется при построении
/// списка (см. ServerSettingsViewModel.OnSelectedRoleChanged) — для admin весь список
/// строится уже неактивным, а не переключается динамически.</summary>
public sealed partial class PermissionOptionViewModel : ObservableObject
{
    public Permission Flag { get; }
    public string Label { get; }
    public bool IsEnabled { get; }

    [ObservableProperty] private bool _isChecked;

    public PermissionOptionViewModel(Permission flag, string label, bool isChecked, bool isEnabled)
    {
        Flag = flag;
        Label = label;
        _isChecked = isChecked;
        IsEnabled = isEnabled;
    }
}
