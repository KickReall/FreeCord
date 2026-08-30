using System;
using System.IO;
using Avalonia.Media.Imaging;

namespace FreeCord.Desktop;

/// <summary>
/// Дисковый кэш аватарок/иконки сервера, ключ включает версию — новую версию просто
/// сохраняем под новым именем файла, старая тихо остаётся сиротой (аватарки маленькие,
/// а сборка мусора по одной картинке на пользователя не стоит усложнения). Версия 0
/// (см. AvatarMessages.h) означает "аватарки нет" — кэш её никогда не хранит.
/// </summary>
public sealed class AvatarCache
{
    private readonly string _dir;

    public AvatarCache(string? dir = null)
    {
        _dir = dir ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "FreeCord", "avatars");
        Directory.CreateDirectory(_dir);
    }

    private string PathFor(string key, long version) => Path.Combine(_dir, $"{Sanitize(key)}_{version}.bin");

    private static string Sanitize(string key)
    {
        foreach (char c in Path.GetInvalidFileNameChars())
            key = key.Replace(c, '_');
        return key;
    }

    public Bitmap? TryLoad(string key, long version)
    {
        if (version <= 0) return null;
        var path = PathFor(key, version);
        if (!File.Exists(path)) return null;
        try
        {
            using var stream = File.OpenRead(path);
            return new Bitmap(stream);
        }
        catch
        {
            return null; // повреждённый файл в кэше — не крашить UI, просто как будто не нашли
        }
    }

    // Сохраняет байты на диск и сразу декодирует — избавляет вызывающий код от
    // необходимости самому оборачивать декодирование в try/catch на каждый вызов.
    public Bitmap? Save(string key, long version, byte[] data)
    {
        if (version <= 0 || data.Length == 0) return null;
        try
        {
            File.WriteAllBytes(PathFor(key, version), data);
            using var stream = new MemoryStream(data);
            return new Bitmap(stream);
        }
        catch
        {
            return null;
        }
    }
}
