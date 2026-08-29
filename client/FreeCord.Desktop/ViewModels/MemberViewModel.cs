using System;
using System.Collections.ObjectModel;
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

    // Своя же строка в списке — тут нет смысла ни переименовывать себя (локальный
    // никнейм и так виден только вам), ни выдавать себе роль (эффективные права
    // не изменятся, если вы уже админ), ни тем более блокировать сами себя.
    public bool IsSelf { get; }
    public bool CanAssignRole { get; }
    public bool CanBlock { get; }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(DisplayName))]
    private string? _localNickname;

    public string DisplayName => string.IsNullOrEmpty(LocalNickname) ? Username : LocalNickname;

    public MemberViewModel(long userId, string username, string? localNickname, bool isSelf,
        bool canManageRoles, bool canManageServerBans, Action<long> block)
    {
        UserId = userId;
        Username = username;
        _localNickname = localNickname;
        IsSelf = isSelf;
        CanAssignRole = canManageRoles && !isSelf;
        CanBlock = canManageServerBans && !isSelf;
        _block = block;
    }

    // Действие "Заблокировать" из контекстного меню — банит IP текущей сессии
    // пользователя; видимость пункта в XAML завязана на CanManageServerBans.
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
