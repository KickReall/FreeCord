using System;
using System.Linq;
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using FreeCord.Protocol;

namespace FreeCord.Desktop.ViewModels;

public partial class MainWindowViewModel : ViewModelBase
{
    private readonly FreeCordConnection _connection = new();

    [ObservableProperty] private string _host = "127.0.0.1";
    [ObservableProperty] private int _port = 6000;
    [ObservableProperty] private string _username = "";
    [ObservableProperty] private string _password = "";
    [ObservableProperty] private string _status = "Not connected";

    // IsLoggedIn управляет тем, какая панель видна: логин или чат
    [ObservableProperty] private bool _isLoggedIn;

    [ObservableProperty] private string _newRoomName = "";
    [ObservableProperty] private string _messageText = "";
    [ObservableProperty] private RoomInfo? _selectedRoom;

    public ObservableCollection<RoomInfo> Rooms { get; } = new();
    public ObservableCollection<string> Messages { get; } = new();

    public MainWindowViewModel()
    {
        // ВАЖНО: события приходят из фонового потока приёма.
        // Трогать ObservableCollection можно только из UI-потока, иначе падение.
        _connection.AuthResponseReceived += r => OnUi(() =>
        {
            if (r.IsSuccess)
            {
                IsLoggedIn = true;
                Status = $"Logged in as {Username} (id={r.UserId})";
                _ = _connection.ListRoomsAsync();
            }
            else Status = "Login failed: wrong username or password";
        });

        _connection.RegisterResponseReceived += r => OnUi(() =>
            Status = r.IsSuccess ? "Registered, now log in" : "Registration failed: username taken");

        _connection.RoomCreated += r => OnUi(() =>
        {
            // Защита от дубликата: список мог обновиться другим путём
            if (Rooms.Any(room => room.Id == r.RoomId)) return;
            Rooms.Add(new RoomInfo(r.RoomId, r.Name));
        });

        _connection.RoomListReceived += r => OnUi(() =>
        {
            Rooms.Clear();
            foreach (var room in r.Rooms) Rooms.Add(room);
        });

        _connection.RoomCreateResponseReceived += r => OnUi(() =>
        {
            Status = r.IsSuccess ? $"Room created (id={r.RoomId})" : "Room name already taken";
            if (r.IsSuccess) _ = _connection.ListRoomsAsync();
        });

        _connection.JoinResponseReceived += r => OnUi(() =>
            Status = r.Status switch
            {
                0 => "Joined room",
                1 => "Room not found",
                2 => "Already a member",
                _ => $"Join error {r.Status}"
            });

        _connection.HistoryReceived += r => OnUi(() =>
        {
            Messages.Clear();
            foreach (var m in r.Messages) Messages.Add($"user{m.SenderId}: {m.Text}");
        });

        _connection.MessageReceived += m => OnUi(() =>
        {
            // Показываем только если сообщение из открытой сейчас комнаты
            if (m.RoomId != SelectedRoom?.Id) return;
            Messages.Add($"{m.SenderName}: {m.Text}");
        });

        _connection.Disconnected += ex => OnUi(() =>
        {
            IsLoggedIn = false;
            Status = $"Disconnected: {ex?.Message ?? "connection closed"}";
        });
    }

    private static void OnUi(Action action) => Dispatcher.UIThread.Post(action);

    private async Task EnsureConnectedAsync()
    {
        if (_connection.IsConnected) return;
        Status = "Connecting...";
        await _connection.ConnectAsync(Host, Port);
        Status = "Connected";
    }

    [RelayCommand]
    private async Task LoginAsync()
    {
        try
        {
            await EnsureConnectedAsync();
            await _connection.LoginAsync(Username, Password);
        }
        catch (Exception ex) { Status = $"Error: {ex.Message}"; }
    }

    [RelayCommand]
    private async Task RegisterAsync()
    {
        try
        {
            await EnsureConnectedAsync();
            await _connection.RegisterAsync(Username, Password);
        }
        catch (Exception ex) { Status = $"Error: {ex.Message}"; }
    }

    [RelayCommand]
    private async Task CreateRoomAsync()
    {
        if (string.IsNullOrWhiteSpace(NewRoomName)) return;
        await _connection.CreateRoomAsync(NewRoomName);
        NewRoomName = "";
    }

    [RelayCommand]
    private async Task JoinSelectedRoomAsync()
    {
        if (SelectedRoom is null) return;
        await _connection.JoinRoomAsync(SelectedRoom.Id);
    }

    [RelayCommand]
    private async Task SendMessageAsync()
    {
        if (SelectedRoom is null || string.IsNullOrWhiteSpace(MessageText)) return;
        await _connection.SendTextAsync(SelectedRoom.Id, MessageText);
        MessageText = "";
    }

    // Срабатывает при клике по комнате в списке
    partial void OnSelectedRoomChanged(RoomInfo? value)
    {
        Messages.Clear();

        if (value is null)
        {
            _ = _connection.LeaveRoomAsync(0);
            return;
        }

        _ = SwitchToRoomAsync(value.Id);
    }

    private async Task SwitchToRoomAsync(long roomId)
    {
        await _connection.JoinRoomAsync(roomId);
        await _connection.RequestHistoryAsync(roomId);
    }
}