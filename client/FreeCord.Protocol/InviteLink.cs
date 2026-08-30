namespace FreeCord.Protocol;

/// <summary>
/// Инвайт-ссылка — чисто клиентский шорткат подключения, не серверная фича:
/// кодирует host:port (и опционально отпечаток сертификата для TOFU), ничего
/// не проверяет и не выдаёт на сервере. Формат: freecord://host:port?fp=XX:XX:...
/// </summary>
public static class InviteLink
{
    private const string Scheme = "freecord://";

    public static string Build(string host, int port, string? fingerprint)
    {
        var link = $"{Scheme}{host}:{port}";
        if (!string.IsNullOrEmpty(fingerprint))
            link += $"?fp={Uri.EscapeDataString(fingerprint)}";
        return link;
    }

    public static bool TryParse(string input, out string host, out int port, out string? fingerprint)
    {
        host = "";
        port = 0;
        fingerprint = null;

        if (string.IsNullOrWhiteSpace(input) || !input.StartsWith(Scheme, StringComparison.OrdinalIgnoreCase))
            return false;

        if (!Uri.TryCreate(input, UriKind.Absolute, out var uri) || uri.Port <= 0)
            return false;

        host = uri.Host;
        port = uri.Port;

        if (!string.IsNullOrEmpty(uri.Query))
        {
            foreach (var pair in uri.Query.TrimStart('?').Split('&', StringSplitOptions.RemoveEmptyEntries))
            {
                var kv = pair.Split('=', 2);
                if (kv.Length == 2 && kv[0] == "fp")
                    fingerprint = Uri.UnescapeDataString(kv[1]);
            }
        }

        return true;
    }
}
