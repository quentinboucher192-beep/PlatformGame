-- 2.44.1: how far a server-side copy has gone (0..1) and what it is doing, written by the copy
-- after each table and each page of files, read by ADMINISTRATION › Sauvegardes (progress bar).
ALTER TABLE backups ADD COLUMN progress REAL NOT NULL DEFAULT 0;
ALTER TABLE backups ADD COLUMN step TEXT;
UPDATE backups SET progress = 1 WHERE status = 'ready';
