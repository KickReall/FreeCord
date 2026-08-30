using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using FreeCord.Protocol;

namespace FreeCord.Desktop.ViewModels;

/// <summary>
/// Один пользователь в панели участников. LocalNickname — чисто клиентская подмена
/// отображаемого имени (см. LocalNicknameStore), не имеет отношения к серверу.
/// </summary>
public partial class MemberViewModel : ObservableObject
{
    private readonly Action<long> _block;

    public long UserId { get; }
    public string Username { get; }
    public IReadOnlyList<long> RoleIds { get; }

    // Проставляет gateway (см. RoleMessages.h) — auth ничего не знает о живых сессиях.
    public bool IsOnline { get; }

    // Тот же принцип — заполняет только gateway из текущей сессии, пусто пока оффлайн.
    // Нужен только вкладке "Пользователи" в настройках сервера, но хранить его здесь
    // проще, чем заводить для одной вкладки отдельную модель строки.
    public string Ip { get; }

    // Отображаемые имена всех назначенных ролей через запятую — для вкладки
    // "Пользователи" (в отличие от группировки по ОДНОЙ, самой сильной роли
    // в основной панели участников, здесь нужен полный список).
    public string RoleNames { get; }

    // Цвет точки-индикатора на аватарке. Считается здесь, а не через XAML-конвертер/
    // Classes-биндинг — тот вариант не сработал на практике (индикатор оставался серым
    // даже для подключённых пользователей), а обычный Binding на готовый Brush гарантированно работает.
    public IBrush PresenceBrush => IsOnline ? Brushes.LimeGreen : new SolidColorBrush(Color.Parse("#747F8D"));

    // Своя же строка в списке — тут нет смысла ни переименовывать себя (локальный
    // никнейм и так виден только вам), ни выдавать себе роль (эффективные права
    // не изменятся, если вы уже админ), ни тем более блокировать/удалять сами себя.
    public bool IsSelf { get; }

    // Владелец (RoleIds.Owner) неприкосновенен для действий кого угодно, кроме
    // самого себя (тот случай уже покрыт IsSelf выше) — сервер всё равно откажет,
    // но прятать эти пункты и на клиенте, чтобы не провоцировать заведомо неудачный запрос.
    public bool IsOwner { get; }
    public bool CanManageRoles { get; }
    public bool CanBlock { get; }
    public bool CanDelete { get; }

    // Загружается асинхронно после конструирования (см. ServerSessionViewModel.ApplyOrFetchAvatar) —
    // либо сразу из дискового кэша, либо по итогам AvatarFetchResponse. null — аватарки нет
    // или ещё не подгрузилась; тогда в UI остаётся круг-заглушка.
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasAvatar))]
    private Bitmap? _avatar;

    public bool HasAvatar => Avatar is not null;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(DisplayName))]
    private string? _localNickname;

    public string DisplayName => string.IsNullOrEmpty(LocalNickname) ? Username : LocalNickname;

    public MemberViewModel(long userId, string username, IReadOnlyList<long> roleIds, bool online, string ip,
        IReadOnlyList<RoleInfo> allRoles, string? localNickname,
        bool isSelf, bool canManageRoles, bool canManageServerBans, bool canManageUsers, Action<long> block)
    {
        UserId = userId;
        Username = username;
        RoleIds = roleIds;
        IsOnline = online;
        Ip = ip;
        RoleNames = string.Join(", ", allRoles.Where(r => roleIds.Contains(r.Id)).Select(r => r.DisplayName));
        _localNickname = localNickname;
        IsSelf = isSelf;
        IsOwner = roleIds.Contains(FreeCord.Protocol.RoleIds.Owner);
        CanManageRoles = canManageRoles && !isSelf && !IsOwner;
        CanBlock = canManageServerBans && !isSelf && !IsOwner;
        CanDelete = canManageUsers && !isSelf && !IsOwner;
        _block = block;
    }

    // Действие "Заблокировать" из контекстного меню — банит IP текущей сессии
    // пользователя; видимость пункта в XAML завязана на CanBlock.
    [RelayCommand]
    private void Block() => _block(UserId);
}

/// <summary>Группа участников с одной ролью — заголовок берётся из RoleInfo.DisplayName.</summary>
public sealed class RoleGroupViewModel
{
    public string Header { get; }
    public ObservableCollection<MemberViewModel> Members { get; } = new();

    public RoleGroupViewModel(string header) => Header = header;
}
