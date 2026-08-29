using System.Collections.Specialized;
using System.ComponentModel;
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
            _subscribedServer.MemberRenameRequested -= OnMemberRenameRequested;
            _subscribedServer.MemberAssignRoleRequested -= OnMemberAssignRoleRequested;
        }

        _subscribedServer = server;

        if (_subscribedServer is not null)
        {
            _subscribedServer.Messages.CollectionChanged += OnMessagesChanged;
            _subscribedServer.MemberRenameRequested += OnMemberRenameRequested;
            _subscribedServer.MemberAssignRoleRequested += OnMemberAssignRoleRequested;
        }
    }

    private void OnMessagesChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.Action != NotifyCollectionChangedAction.Add) return;

        // Прокрутка после того, как элемент отрисуется — иначе высота ещё не пересчитана
        Dispatcher.UIThread.Post(() =>
        {
            var scroll = this.FindControl<ScrollViewer>("MessagesScroll");
            scroll?.ScrollToEnd();
        }, DispatcherPriority.Background);
    }
}
