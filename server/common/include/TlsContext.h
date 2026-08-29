#pragma once
#include <string>
#include <openssl/ssl.h>

// Держит SSL_CTX gateway'я: сертификат и приватный ключ сервера.
// Если файлы certPath/keyPath уже существуют — загружает их. Если нет —
// сама генерирует самоподписанный сертификат (RSA-2048, 10 лет) и сохраняет
// в эти файлы, чтобы при следующем запуске использовался тот же сертификат:
// клиенты закрепляют его отпечаток при первом подключении (TOFU), и смена
// сертификата на каждый рестарт сделала бы это закрепление бессмысленным.
class TlsContext {
public:
    TlsContext(const std::string& certPath, const std::string& keyPath);
    ~TlsContext();

    SSL_CTX* Get() const { return m_ctx; }

    // SHA-256 отпечаток сертификата в виде "aa:bb:cc:...", для вывода в лог при старте.
    const std::string& FingerprintHex() const { return m_fingerprintHex; }

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

private:
    void GenerateSelfSignedCertificate(const std::string& certPath, const std::string& keyPath);
    void ComputeFingerprint();

    SSL_CTX* m_ctx = nullptr;
    std::string m_fingerprintHex;
};
