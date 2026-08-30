using System.Text.Json;

namespace FreeCord.Protocol;

/// <summary>
/// Хранилище отпечатков TLS-сертификатов серверов, которым клиент уже доверял
/// (TOFU — trust on first use, как у SSH known_hosts). При первом подключении
/// к адресу отпечаток запоминается; при последующих — сверяется, и расхождение
/// означает возможную подмену сервера, а не молчаливое переподключение.
/// </summary>
public sealed class TrustedServerStore
{
    private readonly string _path;
    private readonly Dictionary<string, string> _fingerprints;

    public TrustedServerStore(string? path = null)
    {
        _path = path ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "FreeCord", "known_servers.json");
        _fingerprints = Load();
    }

    public string? GetPinnedFingerprint(string host, int port) =>
        _fingerprints.TryGetValue(Key(host, port), out var fingerprint) ? fingerprint : null;

    public void Pin(string host, int port, string fingerprint)
    {
        _fingerprints[Key(host, port)] = fingerprint;
        Save();
    }

    private static string Key(string host, int port) => $"{host}:{port}";

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
            // Битый или недоступный файл — не блокируем подключение, просто начинаем с чистого TOFU-состояния.
            return new();
        }
    }

    private void Save()
    {
        var dir = Path.GetDirectoryName(_path);
        if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
        File.WriteAllText(_path, JsonSerializer.Serialize(_fingerprints));
    }
}
