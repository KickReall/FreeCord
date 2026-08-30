namespace FreeCord.Protocol;

/// <summary>
/// Сертификат сервера не совпал с ранее закреплённым (TOFU) отпечатком.
/// Это не обычная ошибка сети — либо сервер переустановлен с новым
/// сертификатом, либо соединение подменяется (MITM). Решение — за пользователем,
/// поэтому это отдельный тип исключения, а не общий сбой аутентификации.
/// </summary>
public sealed class ServerCertificateChangedException : Exception
{
    public string Host { get; }
    public int Port { get; }
    public string ExpectedFingerprint { get; }
    public string ActualFingerprint { get; }

    public ServerCertificateChangedException(string host, int port, string expectedFingerprint, string actualFingerprint)
        : base($"Сертификат сервера {host}:{port} изменился! Ожидался {expectedFingerprint}, " +
               $"получен {actualFingerprint}. Возможна подмена сервера — соединение отклонено.")
    {
        Host = host;
        Port = port;
        ExpectedFingerprint = expectedFingerprint;
        ActualFingerprint = actualFingerprint;
    }
}
