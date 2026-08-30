#include "TlsTransport.h"
#include <sstream>
#include <iomanip>
#include <openssl/err.h>
#include <openssl/x509.h>

namespace {
    std::string FormatFingerprint(const unsigned char* digest, unsigned int len) {
        std::ostringstream oss;
        for (unsigned int i = 0; i < len; ++i) {
            if (i > 0) oss << ':';
            oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
        }
        return oss.str();
    }
}

TlsTransport::TlsTransport(SSL* ssl, SSL_CTX* ownedCtx)
    : m_ssl(ssl), m_ownedCtx(ownedCtx) {
}

TlsTransport::~TlsTransport() {
    if (m_ssl) {
        SSL_shutdown(m_ssl);
        SSL_free(m_ssl);
    }
    if (m_ownedCtx) {
        SSL_CTX_free(m_ownedCtx);
    }
}

std::unique_ptr<TlsTransport> TlsTransport::AcceptServer(SSL_CTX* ctx, socket_t socket) {
    SSL* ssl = SSL_new(ctx);
    if (!ssl) return nullptr;

    SSL_set_fd(ssl, static_cast<int>(socket));
    if (SSL_accept(ssl) <= 0) {
        SSL_free(ssl);
        return nullptr;
    }

    return std::unique_ptr<TlsTransport>(new TlsTransport(ssl, nullptr));
}

std::unique_ptr<TlsTransport> TlsTransport::ConnectClientNoVerify(socket_t socket, std::string& outFingerprintHex) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return nullptr;
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    // Сертификат самоподписанный, доверенного CA нет — проверка цепочки тут
    // намеренно не делается. Это отладочный клиент (test_client), не GUI.
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        return nullptr;
    }

    SSL_set_fd(ssl, static_cast<int>(socket));
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        return nullptr;
    }

    X509* peerCert = SSL_get1_peer_certificate(ssl);
    if (peerCert) {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestLen = 0;
        if (X509_digest(peerCert, EVP_sha256(), digest, &digestLen) == 1) {
            outFingerprintHex = FormatFingerprint(digest, digestLen);
        }
        X509_free(peerCert);
    }

    return std::unique_ptr<TlsTransport>(new TlsTransport(ssl, ctx));
}

int TlsTransport::Send(const uint8_t* data, int size) {
    int result = SSL_write(m_ssl, data, size);
    return result > 0 ? result : -1;
}

int TlsTransport::Recv(uint8_t* data, int size) {
    int result = SSL_read(m_ssl, data, size);
    if (result > 0) {
        m_lastRecvTimedOut = false;
        return result;
    }

    int sslError = SSL_get_error(m_ssl, result);
    if (sslError == SSL_ERROR_ZERO_RETURN) {
        m_lastRecvTimedOut = false;
        return 0;
    }

    // SSL_ERROR_SYSCALL — смотрим на реальную ОС-ошибку под ним. Блокирующий
    // сокет с SO_RCVTIMEO отдаёт таймаут через тот же код, что и у обычного recv().
    m_lastRecvTimedOut = IsTimeoutError(GetLastSocketError());
    return -1;
}

bool TlsTransport::LastRecvTimedOut() {
    return m_lastRecvTimedOut;
}
