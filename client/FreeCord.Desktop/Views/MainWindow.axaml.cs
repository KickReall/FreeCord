using System.Collections.Specialized;
using Avalonia.Controls;
using Avalonia.Threading;
using FreeCord.Desktop.ViewModels;

namespace FreeCord.Desktop.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, System.EventArgs e)
    {
        if (DataContext is not MainWindowViewModel vm) return;

        // Подписываемся на изменения коллекции, чтобы прокручивать вниз при новом сообщении
        vm.Messages.CollectionChanged += OnMessagesChanged;
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