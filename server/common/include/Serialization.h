#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// --- Строки: uint16_t длина (в байтах, UTF-8) + сами байты, без null-terminator ---

inline void WriteString(std::vector<uint8_t>& buffer, const std::string& str) {
    uint16_t len = static_cast<uint16_t>(str.size());
    const uint8_t* lenBytes = reinterpret_cast<const uint8_t*>(&len);
    buffer.insert(buffer.end(), lenBytes, lenBytes + sizeof(len));
    buffer.insert(buffer.end(), str.begin(), str.end());
}

inline std::string ReadString(const std::vector<uint8_t>& buffer, size_t& offset) {
    uint16_t len = 0;
    std::memcpy(&len, buffer.data() + offset, sizeof(len));
    offset += sizeof(len);

    std::string result(reinterpret_cast<const char*>(buffer.data() + offset), len);
    offset += len;
    return result;
}

// --- Числа фиксированного размера ---
// Шаблон вместо отдельных функций на каждый тип: один код для uint8_t/uint32_t/uint64_t/int64_t.

template <typename T>
inline void WriteScalar(std::vector<uint8_t>& buffer, T value) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
}

template <typename T>
inline T ReadScalar(const std::vector<uint8_t>& buffer, size_t& offset) {
    T value{};
    std::memcpy(&value, buffer.data() + offset, sizeof(T));
    offset += sizeof(T);
    return value;
}