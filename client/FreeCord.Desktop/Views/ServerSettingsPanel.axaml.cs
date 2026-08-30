using Avalonia.Controls;
using Avalonia.Interactivity;
using FreeCord.Desktop.ViewModels;
using FreeCord.Protocol;

namespace FreeCord.Desktop.Views;

// Встроенная панель (не отдельное окно) — оверлей поверх MainWindow задаёт сама
// MainWindow.axaml, эта панель просто показывает вкладки текущего ServerSettingsViewModel,
// который приходит через обычный DataContext-биндинг снаружи (см. ActiveServerSettings
// на MainWindowViewModel), а не через параметр конструктора.
public partial class ServerSettingsPanel : UserControl
{
    public ServerSettingsPanel()
    {
        InitializeComponent();
    }

    // UserControl всё равно лежит внутри MainWindow к моменту клика, поэтому
    // TopLevel.GetTopLevel(this) корректно находит его и без своего окна.
    private async void OnUploadIconClick(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not ServerSettingsViewModel vm) return;
        var data = await ImagePicker.PickBytesAsync(TopLevel.GetTopLevel(this));
        if (data is not null) await vm.Server.UploadServerIconAsync(data);
    }

    // Комбобокс сбрасывается сразу после выбора — это не "текущая роль", а разовое
    // действие "назначить эту роль"; сервер сам ответит, если роль уже была назначена.
    private void OnAssignRoleSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (sender is not ComboBox { SelectedItem: RoleInfo role, DataContext: MemberViewModel member } combo) return;
        combo.SelectedItem = null;
        if (DataContext is ServerSettingsViewModel vm) _ = vm.Server.AssignRoleAsync(member.UserId, role.Id);
    }

    // Список ролей для выбора не отфильтрован до "тех, что реально есть у пользователя" —
    // сервер и так вернёт понятный статус "не была назначена", упрощение того стоит.
    private void OnRemoveRoleSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (sender is not ComboBox { SelectedItem: RoleInfo role, DataContext: MemberViewModel member } combo) return;
        combo.SelectedItem = null;
        if (DataContext is ServerSettingsViewModel vm) _ = vm.Server.RemoveRoleAsync(member.UserId, role.Id);
    }

    // Без подтверждения — банит IP текущей сессии, действие обратимое (список
    // заблокированных IP тут же, в этой вкладке).
    private void OnBlockUserClick(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button { DataContext: MemberViewModel member }) return;
        if (DataContext is ServerSettingsViewModel vm) _ = vm.Server.BanUserSessionAsync(member.UserId);
    }

    // Необратимо (сносит аккаунт целиком) — единственное действие в этой вкладке
    // с подтверждающим диалогом.
    private async void OnDeleteUserClick(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button { DataContext: MemberViewModel member }) return;
        if (DataContext is not ServerSettingsViewModel vm) return;
        if (TopLevel.GetTopLevel(this) is not Window owner) return;

        var dialog = new ConfirmDialog("Удалить пользователя",
            $"Удалить аккаунт «{member.Username}» целиком? Это действие необратимо: пользователь потеряет доступ, " +
            "а его сессия (если он сейчас в сети) будет немедленно отключена. История сообщений останется.",
            "Удалить");
        var confirmed = await dialog.ShowDialog<bool>(owner);
        if (confirmed) await vm.Server.DeleteUserAsync(member.UserId);
    }
}
