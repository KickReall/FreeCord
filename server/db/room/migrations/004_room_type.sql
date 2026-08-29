-- Задел на будущее (см. CLAUDE.md, план "Голос и видео"): деление каналов на
-- текстовые/голосовые. 0 = текстовый, 1 = голосовой — совпадает с RoomType в
-- server/common/include/RoomMessages.h и client/FreeCord.Protocol/RoomType.cs.
-- Создание голосовых каналов пока не выставлено наружу (RoomCreateRequest всегда
-- создаёт текстовый) — сама фича голоса не реализована, это только модель данных.
ALTER TABLE rooms ADD COLUMN type INTEGER NOT NULL DEFAULT 0;
