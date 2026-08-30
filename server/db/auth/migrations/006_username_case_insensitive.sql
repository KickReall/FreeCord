-- Без этого "kirill" и "Kirill" проходили как два разных логина — исходный UNIQUE
-- на username (001_initial.sql) использует BINARY-сравнение по умолчанию, поэтому
-- регистр не учитывался. Отдельный уникальный индекс с COLLATE NOCASE — самый
-- дешёвый способ добавить регистронезависимую уникальность без пересоздания таблицы
-- (SQLite не даёт поменять коллацию существующей колонки через ALTER TABLE).
-- Логин теперь тоже регистронезависим (find_by_username.sql — WHERE ... COLLATE NOCASE),
-- иначе "Kirill" при регистрации и "kirill" при попытке зайти считались бы разными людьми.
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_username_nocase ON users (username COLLATE NOCASE);
