CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id INTEGER NOT NULL,
    sender_id INTEGER NOT NULL,
    sender_name TEXT NOT NULL DEFAULT '',
    text TEXT NOT NULL,
    timestamp INTEGER NOT NULL
);

-- Индекс критичен: без него выборка истории комнаты будет сканировать всю таблицу целиком.
CREATE INDEX IF NOT EXISTS idx_messages_room_time ON messages(room_id, timestamp);
