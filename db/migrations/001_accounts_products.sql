BEGIN;
CREATE TABLE IF NOT EXISTS schema_migrations (
    version integer PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS users (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    username text COLLATE "C" NOT NULL UNIQUE
        CHECK (username ~ '^[A-Za-z0-9_]{3,32}$'),
    display_name text NOT NULL CHECK (octet_length(display_name) BETWEEN 1 AND 96),
    password_hash text NOT NULL CHECK (password_hash LIKE '$argon2id$%' AND octet_length(password_hash) < 128),
    created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS products (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name text NOT NULL CHECK (octet_length(name) BETWEEN 1 AND 96),
    image_key text NOT NULL UNIQUE CHECK (
        octet_length(image_key) BETWEEN 1 AND 64 AND
        image_key ~ '^[A-Za-z0-9_./-]+$' AND
        image_key NOT LIKE '/%' AND position('..' in image_key) = 0),
    price_vnd bigint NOT NULL CHECK (price_vnd BETWEEN 1 AND 1000000000000),
    active boolean NOT NULL DEFAULT true
);
INSERT INTO schema_migrations(version) VALUES (1) ON CONFLICT DO NOTHING;
COMMIT;
