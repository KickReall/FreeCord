#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct ControlHeader {
    uint32_t length;       // длина payload В БАЙТАХ, без учёта самого заголовка
    uint16_t messageType;
    uint32_t sequence;     // для сопоставления запрос/ответ, пока не обязателен к использованию
};
#pragma pack(pop)

constexpr size_t CONTROL_HEADER_SIZE = sizeof(ControlHeader);