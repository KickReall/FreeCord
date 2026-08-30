using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace FreeCord.Desktop;

/// <summary>
/// Список серверов, к которым пользователь когда-либо успешно подключался —
/// показывается в левой панели между перезапусками приложения. Пароли и токены
/// сюда не пишутся: при следующем запуске сервер просто виден в списке, вход
/// нужно выполнить заново.
/// </summary>
public sealed record ServerEntry(string Host, int Port, string DisplayName);

public sealed class ServerListStore
{
    private readonly string _path;

    public ServerListStore(string? path = null)
    {
        _path = path ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "FreeCord", "servers.json");
    }

    public List<ServerEntry> Load()
    {
        try
        {
            if (!File.Exists(_path)) return new();
            var json = File.ReadAllText(_path);
            return JsonSerializer.Deserialize<List<ServerEntry>>(json) ?? new();
        }
        catch
        {
            // Битый или недоступный файл — не блокируем запуск, начинаем с пустого списка.
            return new();
        }
    }

    public void Save(IReadOnlyList<ServerEntry> entries)
    {
        var dir = Path.GetDirectoryName(_path);
        if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
        File.WriteAllText(_path, JsonSerializer.Serialize(entries));
    }
}
