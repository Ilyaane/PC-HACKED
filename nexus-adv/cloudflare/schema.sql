CREATE TABLE IF NOT EXISTS messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  sender TEXT NOT NULL CHECK(sender IN ('alice','bob')),
  recipient TEXT NOT NULL CHECK(recipient IN ('alice','bob')),
  kind TEXT NOT NULL CHECK(kind IN ('text','voice')),
  body_text TEXT,
  body_blob BLOB,
  client_id TEXT NOT NULL,
  created INTEGER NOT NULL,
  read INTEGER NOT NULL DEFAULT 0,
  UNIQUE(sender,client_id)
);
CREATE INDEX IF NOT EXISTS messages_inbox ON messages(recipient,id);