using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace FreeCord.Desktop.Views;

// Небольшой переиспользуемый диалог "ввести строку" — используется и для переименования
// сервера в рейле, и для локального никнейма участника; Avalonia не даёт готового InputBox.
public partial class TextPromptDialog : Window
{
    public TextPromptDialog()
    {
        InitializeComponent();
    }

    public TextPromptDialog(string title, string label, string currentValue) : this()
    {
        Title = title;
        this.FindControl<TextBlock>("LabelText")!.Text = label;
        var box = this.FindControl<TextBox>("ValueBox")!;
        box.Text = currentValue;
        box.SelectAll();
    }

    private void OnValueBoxKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) Accept();
        else if (e.Key == Key.Escape) Close(null);
    }

    private void OnOk(object? sender, RoutedEventArgs e) => Accept();

    private void OnCancel(object? sender, RoutedEventArgs e) => Close(null);

    private void Accept()
    {
        var box = this.FindControl<TextBox>("ValueBox")!;
        Close(box.Text);
    }
}
