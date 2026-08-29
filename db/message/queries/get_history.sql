SELECT id, room_id, sender_id, sender_name, timestamp, text FROM messages
WHERE room_id = ? ORDER BY timestamp DESC, id DESC LIMIT ?
