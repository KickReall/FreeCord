CREATE TABLE IF NOT EXISTS roles (
    id INTEGER PRIMARY KEY,
    name TEXT UNIQUE NOT NULL,
    is_system INTEGER NOT NULL DEFAULT 0,
    permissions INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS user_roles (
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    PRIMARY KEY (user_id, role_id)
);

-- id совпадают с kAdminRoleId/kGuestRoleId в server/common/include/Permissions.h.
-- У admin (id=1) права не хранятся маской — код всегда считает его суперпользователем,
-- поэтому здесь 0, а не сумма всех текущих битов (не пришлось бы держать её в синхроне
-- при добавлении новых прав). У guest (id=2) — 7 = ViewChannel | OpenChannel | SendMessages.
INSERT OR IGNORE INTO roles (id, name, is_system, permissions) VALUES (1, 'admin', 1, 0);
INSERT OR IGNORE INTO roles (id, name, is_system, permissions) VALUES (2, 'guest', 1, 7);
