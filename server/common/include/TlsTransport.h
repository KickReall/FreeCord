#pragma once
#include <memory>
#include <string>
#include <openssl/ssl.h>
#include "Transport.h"
#include "PlatformSocket.h"

// TLS поверх уже установленного TCP-соединения. Таймауты определяются так же,
// как у PlainTransport: SO_RCVTIMEO стоит на самом сокете под TLS, и блокирующий
// SSL_read под таймаутом падает с той же ОС-ошибкой (EWOULDBLOCK/WSAETIMEDOUT) —
// IsTimeoutError() из PlatformSocket работает без изменений.
class TlsTransport : public ITransport {
public:
    ~TlsTransport() override;

    // Серверное рукопожатие (SSL_accept) над только что принятым сокетом.
    // nullptr, если handshake не удался — вызывающий код просто закрывает это
    // одно соединение и продолжает слушать остальные.
    static std::unique_ptr<TlsTransport> AcceptServer(SSL_CTX* ctx, socket_t socket);

    // Клиентское рукопожатие (SSL_connect) без проверки цепочки сертификата —
    // используется только test_client'ом (отладочный инструмент, без TOFU/pinning).
    // outFingerprintHex получает SHA-256 отпечаток сертификата сервера в формате "AA:BB:...".
    static std::unique_ptr<TlsTransport> ConnectClientNoVerify(socket_t socket, std::string& outFingerprintHex);

    int Send(const uint8_t* data, int size) override;
    int Recv(uint8_t* data, int size) override;
    bool LastRecvTimedOut() override;

    TlsTransport(const TlsTransport&) = delete;
    TlsTransport& operator=(const TlsTransport&) = delete;

private:
    TlsTransport(SSL* ssl, SSL_CTX* ownedCtx);

    SSL* m_ssl;
    SSL_CTX* m_ownedCtx; // ненулевой только для клиентского пути — свой ephemeral CTX, который надо освободить
    bool m_lastRecvTimedOut = false;
};
