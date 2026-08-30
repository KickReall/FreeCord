-- role_id ссылается на roles.id в БД auth_service — FK невозможен, это отдельный
-- файл БД. Денормализация того же рода, что sender_name в messages.
CREATE TABLE IF NOT EXISTS channel_role_overrides (
    room_id INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    role_id INTEGER NOT NULL,
    allow INTEGER NOT NULL DEFAULT 0,
    deny INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (room_id, role_id)
);
