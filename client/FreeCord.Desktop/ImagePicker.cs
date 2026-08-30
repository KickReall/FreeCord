using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace FreeCord.Desktop;

/// <summary>Общий файловый диалог выбора картинки — используется и панелью участников
/// (своя аватарка), и окном настроек сервера (аватарка/иконка). Размер и формат
/// проверяет сервер, клиент отдаёт байты как есть.</summary>
public static class ImagePicker
{
    public static async Task<byte[]?> PickBytesAsync(TopLevel? topLevel)
    {
        var storage = topLevel?.StorageProvider;
        if (storage is null) return null;

        var files = await storage.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Выберите изображение",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("Изображения") { Patterns = new[] { "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp" } }
            }
        });
        if (files.Count == 0) return null;

        await using var stream = await files[0].OpenReadAsync();
        using var ms = new MemoryStream();
        await stream.CopyToAsync(ms);
        return ms.ToArray();
    }
}
