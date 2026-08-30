using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using FreeCord.Desktop.ViewModels;
using FreeCord.Protocol;

namespace FreeCord.Desktop.Views;

public partial class MainWindow : Window
{
    private ServerSessionViewModel? _subscribedServer;

    public MainWindow()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, System.EventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm) return;

        vm.PropertyChanged += OnViewModelPropertyChanged;
        vm.RenameRequested += OnRenameRequested;
        vm.CopyInviteLinkRequested += OnCopyInviteLinkRequested;
        SubscribeToActiveServer(vm.ActiveServer);
    }

    // Диалог переименования — часть View, поэтому открывается здесь, а не во ViewModel
    private async void OnRenameRequested(ServerSessionViewModel server)
    {
        var dialog = new TextPromptDialog("Переименовать сервер", "Название сервера", server.DisplayName);
        var result = await dialog.ShowDialog<string?>(this);
        if (!string.IsNullOrWhiteSpace(result))
            server.DisplayName = result.Trim();
    }

    // Локальный никнейм участника — виден только у меня, ни на что на сервере не влияет.
    // Пустая строка = сброс к настоящему имени пользователя.
    private async void OnMemberRenameRequested(MemberViewModel member)
    {
        if (_subscribedServer is null) return;
        var dialog = new TextPromptDialog("Локальный никнейм", $"Как вы хотите видеть {member.Username} (только у вас)", member.LocalNickname ?? "");
        var result = await dialog.ShowDialog<string?>(this);
        if (result is null) return;
        _subscribedServer.SetLocalNickname(member.UserId, result.Trim());
    }

    // Диалог выбора роли — как и переименование, это забота View
    private async void OnMemberAssignRoleRequested(MemberViewModel member)
    {
        if (_subscribedServer is null) return;
        var dialog = new RolePickerDialog(_subscribedServer.Roles);
        var role = await dialog.ShowDialog<RoleInfo?>(this);
        if (role is not null) await _subscribedServer.AssignRoleAsync(member.UserId, role.Id);
    }

    // Список для выбора ограничен ролями, которые у пользователя реально есть —
    // снимать роль, которой и так нет, бессмысленно.
    private async void OnMemberRemoveRoleRequested(MemberViewModel member)
    {
        if (_subscribedServer is null) return;
        var currentRoles = _subscribedServer.Roles.Where(r => member.RoleIds.Contains(r.Id)).ToList();
        var dialog = new RolePickerDialog(currentRoles);
        var role = await dialog.ShowDialog<RoleInfo?>(this);
        if (role is not null) await _subscribedServer.RemoveRoleAsync(member.UserId, role.Id);
    }

    // Необратимо (сносит аккаунт целиком) — поэтому единственное действие в панели
    // участников с подтверждающим диалогом.
    private async void OnMemberDeleteRequested(MemberViewModel member)
    {
        if (_subscribedServer is null) return;
        var dialog = new ConfirmDialog("Удалить пользователя",
            $"Удалить аккаунт «{member.Username}» целиком? Это действие необратимо: пользователь потеряет доступ, " +
            "а его сессия (если он сейчас в сети) будет немедленно отключена. История сообщений останется.",
            "Удалить");
        var confirmed = await dialog.ShowDialog<bool>(this);
        if (confirmed) await _subscribedServer.DeleteUserAsync(member.UserId);
    }

    // Буфер обмена — тоже доступен только через TopLevel, поэтому здесь, а не во ViewModel
    private async void OnCopyInviteLinkRequested(ServerSessionViewModel server)
    {
        var link = InviteLink.Build(server.Host, server.Port, server.ServerFingerprint);
        var clipboard = TopLevel.GetTopLevel(this)?.Clipboard;
        if (clipboard is null) return;

        using var transfer = new DataTransfer();
        transfer.Add(DataTransferItem.CreateText(link));
        await clipboard.SetDataAsync(transfer);
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(MainWindowViewModel.ActiveServer)) return;
        if (sender is MainWindowViewModel vm) SubscribeToActiveServer(vm.ActiveServer);
    }

    // Активный сервер меняется при переключении в левой панели — переподписываемся
    // на его ленту сообщений, чтобы автопрокрутка работала для текущего сервера.
    private void SubscribeToActiveServer(ServerSessionViewModel? server)
    {
        if (_subscribedServer is not null)
        {
            _subscribedServer.Messages.CollectionChanged -= OnMessagesChanged;
            _subscribedServer.PropertyChanged -= OnActiveServerPropertyChanged;
            _subscribedServer.MemberRenameRequested -= OnMemberRenameRequested;
            _subscribedServer.MemberAssignRoleRequested -= OnMemberAssignRoleRequested;
            _subscribedServer.MemberRemoveRoleRequested -= OnMemberRemoveRoleRequested;
            _subscribedServer.MemberDeleteRequested -= OnMemberDeleteRequested;
        }

        _subscribedServer = server;

        if (_subscribedServer is not null)
        {
            _subscribedServer.Messages.CollectionChanged += OnMessagesChanged;
            _subscribedServer.PropertyChanged += OnActiveServerPropertyChanged;
            _subscribedServer.MemberRenameRequested += OnMemberRenameRequested;
            _subscribedServer.MemberAssignRoleRequested += OnMemberAssignRoleRequested;
            _subscribedServer.MemberRemoveRoleRequested += OnMemberRemoveRoleRequested;
            _subscribedServer.MemberDeleteRequested += OnMemberDeleteRequested;
        }
    }

    private void OnMessagesChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.Action != NotifyCollectionChangedAction.Add) return;
        ScrollMessagesToEnd();
    }

    // Появление/исчезновение строки "X печатает..." меняет высоту, доступную под
    // ленту (у неё общий родитель-Grid с этой строкой) — без повторного скролла
    // последнее сообщение у самого низа ленты пряталось за индикатором, когда лента
    // уже была прокручена до упора (виден скроллбар).
    private void OnActiveServerPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(ServerSessionViewModel.HasTypingIndicator)) ScrollMessagesToEnd();
    }

    private void ScrollMessagesToEnd()
    {
        // Прокрутка после того, как элемент отрисуется — иначе высота ещё не пересчитана
        Dispatcher.UIThread.Post(() =>
        {
            var scroll = this.FindControl<ScrollViewer>("MessagesScroll");
            scroll?.ScrollToEnd();
        }, DispatcherPriority.Background);
    }
}
