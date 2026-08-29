CREATE TABLE IF NOT EXISTS rooms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE NOT NULL,
    is_system INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Системная комната создаётся один раз при первом запуске.
-- INSERT OR IGNORE: если уже есть — молча ничего не делает.
INSERT OR IGNORE INTO rooms (id, name, is_system) VALUES (1, 'system', 1);

CREATE TABLE IF NOT EXISTS room_members (
    room_id INTEGER NOT NULL REFERENCES rooms(id),
    user_id INTEGER NOT NULL,
    joined_at TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (room_id, user_id)
);
