INSERT INTO channel_role_overrides (room_id, role_id, allow, deny) VALUES (?, ?, ?, ?)
ON CONFLICT(room_id, role_id) DO UPDATE SET allow = excluded.allow, deny = excluded.deny;
