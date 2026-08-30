using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace FreeCord.Desktop;

/// <summary>
/// Локальные никнеймы участников — видны только этому клиенту, никогда не уходят
/// на сервер и не влияют на других пользователей. Ключ — host:port:userId, так как
/// один и тот же userId на разных серверах может быть совершенно разными людьми.
/// </summary>
public sealed class LocalNicknameStore
{
    private readonly string _path;
    private Dictionary<string, string> _nicknames;

    public LocalNicknameStore(string? path = null)
    {
        _path = path ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "FreeCord", "nicknames.json");
        _nicknames = Load();
    }

    public string? Get(string host, int port, long userId) =>
        _nicknames.TryGetValue(Key(host, port, userId), out var nickname) ? nickname : null;

    // Перечитываем файл прямо перед записью, а не полагаемся на снимок, загруженный
    // при старте: если запущено два экземпляра приложения (например, для ручного
    // теста чата "от двух пользователей" на одной машине), у каждого свой процесс
    // и свой независимый _nicknames в памяти — без перечитывания второй save() тупо
    // перезаписал бы файл своим снимком и стёр правки, сделанные первым экземпляром.
    public void Set(string host, int port, long userId, string? nickname)
    {
        _nicknames = Load();
        var key = Key(host, port, userId);
        if (string.IsNullOrEmpty(nickname)) _nicknames.Remove(key);
        else _nicknames[key] = nickname;
        Save();
    }

    private static string Key(string host, int port, long userId) => $"{host}:{port}:{userId}";

    private Dictionary<string, string> Load()
    {
        try
        {
            if (!File.Exists(_path)) return new();
            var json = File.ReadAllText(_path);
            return JsonSerializer.Deserialize<Dictionary<string, string>>(json) ?? new();
        }
        catch
        {
            return new();
        }
    }

    private void Save()
    {
        var dir = Path.GetDirectoryName(_path);
        if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
        File.WriteAllText(_path, JsonSerializer.Serialize(_nicknames));
    }
}
