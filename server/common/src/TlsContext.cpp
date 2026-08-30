#include "TlsContext.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/bio.h>

namespace {

    // EVP_PKEY_CTX-based keygen работает и на OpenSSL 1.1.1, и на 3.x —
    // в отличие от более новых хелперов вроде EVP_RSA_gen, доступных не везде.
    EVP_PKEY* GenerateRsaKey() {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
        if (!ctx) throw std::runtime_error("TLS: EVP_PKEY_CTX_new_id failed");

        EVP_PKEY* pkey = nullptr;
        if (EVP_PKEY_keygen_init(ctx) <= 0 ||
            EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 ||
            EVP_PKEY_keygen(ctx, &pkey) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            throw std::runtime_error("TLS: RSA key generation failed");
        }
        EVP_PKEY_CTX_free(ctx);
        return pkey;
    }

    X509* GenerateSelfSignedX509(EVP_PKEY* pkey) {
        X509* cert = X509_new();
        if (!cert) throw std::runtime_error("TLS: X509_new failed");

        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_get_notBefore(cert), 0);
        X509_gmtime_adj(X509_get_notAfter(cert), 60L * 60 * 24 * 3650); // 10 лет
        X509_set_pubkey(cert, pkey);

        X509_NAME* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>("FreeCord"), -1, -1, 0);
        X509_set_issuer_name(cert, name); // самоподписанный: issuer == subject

        if (X509_sign(cert, pkey, EVP_sha256()) == 0) {
            X509_free(cert);
            throw std::runtime_error("TLS: X509_sign failed");
        }
        return cert;
    }

    std::string FormatFingerprint(const unsigned char* digest, unsigned int len) {
        std::ostringstream oss;
        for (unsigned int i = 0; i < len; ++i) {
            if (i > 0) oss << ':';
            oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
        }
        return oss.str();
    }

    // Пишем PEM в память через BIO и сохраняем на диск сами (std::ofstream), а не
    // отдаём OpenSSL наш FILE* напрямую — на Windows с динамической сборкой OpenSSL
    // (vcpkg) это падает с "OPENSSL_Uplink ... no OPENSSL_Applink", так как FILE*
    // не переживает переход через границу DLL со своим отдельным рантаймом CRT.
    std::string PemFromPrivateKey(EVP_PKEY* pkey) {
        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string result(data, static_cast<size_t>(len));
        BIO_free(bio);
        return result;
    }

    std::string PemFromX509(X509* cert) {
        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_X509(bio, cert);
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string result(data, static_cast<size_t>(len));
        BIO_free(bio);
        return result;
    }

    void WriteFile(const std::string& path, const std::string& content) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("TLS: cannot write " + path);
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

} // namespace

TlsContext::TlsContext(const std::string& certPath, const std::string& keyPath) {
    if (!std::filesystem::exists(certPath) || !std::filesystem::exists(keyPath)) {
        GenerateSelfSignedCertificate(certPath, keyPath);
    }

    m_ctx = SSL_CTX_new(TLS_server_method());
    if (!m_ctx) {
        throw std::runtime_error("TLS: SSL_CTX_new failed");
    }
    SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(m_ctx, certPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        throw std::runtime_error("TLS: failed to load certificate from " + certPath);
    }
    if (SSL_CTX_use_PrivateKey_file(m_ctx, keyPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        throw std::runtime_error("TLS: failed to load private key from " + keyPath);
    }
    if (!SSL_CTX_check_private_key(m_ctx)) {
        throw std::runtime_error("TLS: certificate and private key at \"" + certPath + "\" / \"" + keyPath + "\" do not match");
    }

    ComputeFingerprint();
}

TlsContext::~TlsContext() {
    if (m_ctx) SSL_CTX_free(m_ctx);
}

void TlsContext::GenerateSelfSignedCertificate(const std::string& certPath, const std::string& keyPath) {
    EVP_PKEY* pkey = GenerateRsaKey();
    X509* cert = GenerateSelfSignedX509(pkey);

    std::string keyPem = PemFromPrivateKey(pkey);
    std::string certPem = PemFromX509(cert);

    EVP_PKEY_free(pkey);
    X509_free(cert);

    if (keyPem.empty() || certPem.empty()) {
        throw std::runtime_error("TLS: failed to serialize generated certificate/key");
    }

    WriteFile(keyPath, keyPem);
    WriteFile(certPath, certPem);
}

void TlsContext::ComputeFingerprint() {
    const X509* cert = SSL_CTX_get0_certificate(m_ctx);
    if (!cert) {
        throw std::runtime_error("TLS: no certificate loaded in context");
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    if (X509_digest(cert, EVP_sha256(), digest, &digestLen) != 1) {
        throw std::runtime_error("TLS: failed to compute certificate fingerprint");
    }

    m_fingerprintHex = FormatFingerprint(digest, digestLen);
}
