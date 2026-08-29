ALTER TABLE roles ADD COLUMN display_name TEXT NOT NULL DEFAULT '';
UPDATE roles SET display_name = name WHERE display_name = '';
