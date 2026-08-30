using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

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

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(DisplayName))]
    private string? _localNickname;

    public string DisplayName => string.IsNullOrEmpty(LocalNickname) ? Username : LocalNickname;

    public MemberViewModel(long userId, string username, IReadOnlyList<long> roleIds, bool online, string? localNickname,
        bool isSelf, bool canManageRoles, bool canManageServerBans, bool canManageUsers, Action<long> block)
    {
        UserId = userId;
        Username = username;
        RoleIds = roleIds;
        IsOnline = online;
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
