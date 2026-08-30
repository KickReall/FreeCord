#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Serialization.h"

// Сырые байты — общий payload для AvatarUploadRequest и ServerIconUploadRequest:
// в обоих случаях клиент присылает только сами данные картинки, ничего больше
// (для пользовательской аватарки userId неявный — это всегда сам отправитель,
// подставляет его gateway; иконка сервера одна на весь деплой, id ей не нужен).
struct AvatarBytesPayload {
    std::vector<uint8_t> data;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteBytes(buffer, data);
        return buffer;
    }
    static AvatarBytesPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        AvatarBytesPayload r;
        r.data = ReadBytes(buffer, offset);
        return r;
    }
};

// Общий ответ на загрузку — и своей аватарки, и иконки сервера.
struct AvatarUploadResponsePayload {
    uint8_t status = 0;
    int64_t version = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, status);
        WriteScalar(buffer, version);
        return buffer;
    }
    static AvatarUploadResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        AvatarUploadResponsePayload r;
        r.status = ReadScalar<uint8_t>(buffer, offset);
        r.version = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

// AvatarFetchRequest — тот же тип клиент->gateway и gateway->auth (raw-forward).
struct AvatarFetchRequestPayload {
    int64_t userId = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, userId);
        return buffer;
    }
    static AvatarFetchRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        AvatarFetchRequestPayload r;
        r.userId = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

// version = 0 и пустые данные — у пользователя нет аватарки.
struct AvatarFetchResponsePayload {
    int64_t userId = 0;
    int64_t version = 0;
    std::vector<uint8_t> data;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, userId);
        WriteScalar(buffer, version);
        WriteBytes(buffer, data);
        return buffer;
    }
    static AvatarFetchResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        AvatarFetchResponsePayload r;
        r.userId = ReadScalar<int64_t>(buffer, offset);
        r.version = ReadScalar<int64_t>(buffer, offset);
        r.data = ReadBytes(buffer, offset);
        return r;
    }
};

// Internal only: gateway -> auth, версия подставленного userId уже проверенного gateway'ем.
struct SetUserAvatarRequestPayload {
    int64_t userId = 0;
    std::vector<uint8_t> data;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, userId);
        WriteBytes(buffer, data);
        return buffer;
    }
    static SetUserAvatarRequestPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        SetUserAvatarRequestPayload r;
        r.userId = ReadScalar<int64_t>(buffer, offset);
        r.data = ReadBytes(buffer, offset);
        return r;
    }
};

// Internal only: auth -> gateway.
struct SetUserAvatarResponsePayload {
    uint8_t status = 0;  // 0 = ok, 1 = пользователь не найден
    int64_t version = 0;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, status);
        WriteScalar(buffer, version);
        return buffer;
    }
    static SetUserAvatarResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        SetUserAvatarResponsePayload r;
        r.status = ReadScalar<uint8_t>(buffer, offset);
        r.version = ReadScalar<int64_t>(buffer, offset);
        return r;
    }
};

// Иконка сервера — тот же тип и для ответа на явный запрос, и для пуша всем
// подключённым клиентам сразу после успешной загрузки (см. HandleServerIconUpload).
struct ServerIconFetchResponsePayload {
    int64_t version = 0;
    std::vector<uint8_t> data;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteScalar(buffer, version);
        WriteBytes(buffer, data);
        return buffer;
    }
    static ServerIconFetchResponsePayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        ServerIconFetchResponsePayload r;
        r.version = ReadScalar<int64_t>(buffer, offset);
        r.data = ReadBytes(buffer, offset);
        return r;
    }
};

// Имя и описание сервера — тот же тип и для ответа на явный запрос, и для пуша
// всем сразу после успешного изменения (см. HandleSetServerInfo). Короткие строки,
// поэтому без версии/кэша — перекачивать их каждый раз дешевле, чем кэш городить.
struct ServerInfoPayload {
    std::string name;
    std::string description;

    std::vector<uint8_t> Serialize() const {
        std::vector<uint8_t> buffer;
        WriteString(buffer, name);
        WriteString(buffer, description);
        return buffer;
    }
    static ServerInfoPayload Deserialize(const std::vector<uint8_t>& buffer) {
        size_t offset = 0;
        ServerInfoPayload r;
        r.name = ReadString(buffer, offset);
        r.description = ReadString(buffer, offset);
        return r;
    }
};
