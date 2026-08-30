-- id=3 совпадает с kOwnerRoleId в server/common/include/Permissions.h. Роль owner
-- не про права (permissions=0, ровно как у admin: свои полномочия не хранятся
-- маской) — она про неприкосновенность цели действия, которую отдельно проверяет
-- gateway. Присваивается только первому когда-либо зарегистрированному пользователю
-- (см. UserRepository::CreateUser), в дополнение к admin.
INSERT OR IGNORE INTO roles (id, name, is_system, permissions, display_name) VALUES (3, 'owner', 1, 0, 'Владелец');

-- Бэкфилл для уже существующих БД: на новых установках owner назначает сам
-- UserRepository::CreateUser в момент регистрации первого пользователя, но этот
-- код не выполняется задним числом для тех, кто уже был создан раньше. Роль admin
-- (id=1) в этом проекте и так получает только первый когда-либо зарегистрированный
-- пользователь (см. CreateUser) — поэтому на существующих БД owner можно безопасно
-- отдать всем, кто уже admin, не выбирая по created_at отдельно.
INSERT OR IGNORE INTO user_roles (user_id, role_id)
SELECT user_id, 3 FROM user_roles WHERE role_id = 1;
