namespace FreeCord.Protocol;

/// <summary>
/// Задел на будущее (см. план "Голос и видео" в CLAUDE.md) — деление каналов на
/// текстовые/голосовые. Пока чисто модель данных: создание голосового канала
/// нигде не выставлено наружу, все существующие комнаты — Text. Значения должны
/// совпадать с RoomType в server/common/include/RoomMessages.h.
/// </summary>
public enum RoomType : byte
{
    Text = 0,
    Voice = 1
}
