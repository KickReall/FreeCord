using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.Interactivity;
using FreeCord.Protocol;

namespace FreeCord.Desktop.Views;

public partial class RolePickerDialog : Window
{
    public RolePickerDialog()
    {
        InitializeComponent();
    }

    public RolePickerDialog(IReadOnlyList<RoleInfo> roles) : this()
    {
        this.FindControl<ListBox>("RoleListBox")!.ItemsSource = roles;
    }

    private void OnOk(object? sender, RoutedEventArgs e)
    {
        var listBox = this.FindControl<ListBox>("RoleListBox")!;
        Close(listBox.SelectedItem as RoleInfo);
    }

    private void OnCancel(object? sender, RoutedEventArgs e) => Close(null);
}
