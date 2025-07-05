TABLE users (
    username VARCHAR(64) PRIMARY KEY,
    password_hash BLOB NOT NULL
);