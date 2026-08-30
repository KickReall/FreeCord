using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;

namespace FreeCord.Desktop.Views;

// Диалог подтверждения необратимых действий (сейчас — удаление аккаунта из
// вкладки "Пользователи" в настройках сервера).
public partial class ConfirmDialog : Window
{
    public ConfirmDialog()
    {
        InitializeComponent();
    }

    public ConfirmDialog(string title, string message, string confirmText = "Да") : this()
    {
        Title = title;
        this.FindControl<TextBlock>("MessageText")!.Text = message;
        var confirmButton = this.FindControl<Button>("ConfirmButton")!;
        confirmButton.Content = confirmText;
        confirmButton.Foreground = Brushes.OrangeRed;
    }

    private void OnConfirm(object? sender, RoutedEventArgs e) => Close(true);

    private void OnCancel(object? sender, RoutedEventArgs e) => Close(false);
}
