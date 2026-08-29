#include "PasswordHasher.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <vector>

namespace {
    std::string BytesToHex(const unsigned char* data, size_t len) {
        std::ostringstream oss;
        for (size_t i = 0; i < len; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        }
        return oss.str();
    }

    std::vector<unsigned char> HexToBytes(const std::string& hex) {
        std::vector<unsigned char> bytes;
        for (size_t i = 0; i < hex.size(); i += 2) {
            unsigned char byte = static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16));
            bytes.push_back(byte);
        }
        return bytes;
    }

    constexpr int kSaltBytes = 16;
    constexpr int kHashBytes = 32; // SHA-256 output
    constexpr int kIterations = 100000;
}

namespace PasswordHasher {

    std::string GenerateSalt() {
        unsigned char salt[kSaltBytes];
        RAND_bytes(salt, kSaltBytes);
        return BytesToHex(salt, kSaltBytes);
    }

    std::string HashPassword(const std::string& password, const std::string& saltHex) {
        std::vector<unsigned char> salt = HexToBytes(saltHex);
        unsigned char hash[kHashBytes];

        PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt.data(), static_cast<int>(salt.size()),
            kIterations,
            EVP_sha256(),
            kHashBytes, hash
        );

        return BytesToHex(hash, kHashBytes);
    }

    bool VerifyPassword(const std::string& password, const std::string& saltHex, const std::string& expectedHashHex) {
        std::string actualHash = HashPassword(password, saltHex);
        // Не используем ==, чтобы не давать тайминг-атаку через ранний выход из сравнения строк.
        // Для личного проекта это не критично, но привычка правильная.
        if (actualHash.size() != expectedHashHex.size()) return false;
        unsigned char diff = 0;
        for (size_t i = 0; i < actualHash.size(); ++i) {
            diff |= static_cast<unsigned char>(actualHash[i]) ^ static_cast<unsigned char>(expectedHashHex[i]);
        }
        return diff == 0;
    }

} // namespace PasswordHasher