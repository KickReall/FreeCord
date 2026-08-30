using System.Collections.Specialized;
using System.ComponentModel;
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
        vm.CopyInviteLinkRequested += OnCopyInviteLinkRequested;
        SubscribeToActiveServer(vm.ActiveServer);
    }

    // Локальный никнейм участника — виден только у меня, ни на что на сервере не влияет.
    // Пустая строка = сброс к настоящему имени пользователя. Единственное, что осталось
    // в контекстном меню панели участников — остальные административные действия
    // переехали в оверлей настроек сервера (см. ServerSettingsPanel).
    private async void OnMemberRenameRequested(MemberViewModel member)
    {
        if (_subscribedServer is null) return;
        var dialog = new TextPromptDialog("Локальный никнейм", $"Как вы хотите видеть {member.Username} (только у вас)", member.LocalNickname ?? "");
        var result = await dialog.ShowDialog<string?>(this);
        if (result is null) return;
        _subscribedServer.SetLocalNickname(member.UserId, result.Trim());
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
        }

        _subscribedServer = server;

        if (_subscribedServer is not null)
        {
            _subscribedServer.Messages.CollectionChanged += OnMessagesChanged;
            _subscribedServer.PropertyChanged += OnActiveServerPropertyChanged;
            _subscribedServer.MemberRenameRequested += OnMemberRenameRequested;
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
