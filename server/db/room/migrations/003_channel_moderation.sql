-- Модерация по каналам: бан (используется и для кика — кик = бан + принудительный
-- выход, если пользователь сейчас онлайн в этой комнате) и мут. Оба состояния
-- привязаны к (room_id, user_id) и живут до ручного снятия администратором.
CREATE TABLE IF NOT EXISTS channel_bans (
    room_id INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL,
    PRIMARY KEY (room_id, user_id)
);

CREATE TABLE IF NOT EXISTS channel_mutes (
    room_id INTEGER NOT NULL REFERENCES rooms(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL,
    PRIMARY KEY (room_id, user_id)
);
