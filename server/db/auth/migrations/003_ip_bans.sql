-- Бан по IP на уровне всего сервера — проверяется gateway'ем ещё до TLS-хендшейка,
-- отдельно и независимо от ролей/прав (можно забанить даже незарегистрированный IP).
CREATE TABLE IF NOT EXISTS banned_ips (
    ip TEXT PRIMARY KEY
);
