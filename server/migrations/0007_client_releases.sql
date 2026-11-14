-- Installers of the game offered to players (notification "nouvelle version" in the game).
-- The files live in the R2 bucket (releases/...), served by GET /client/download/{version}.
CREATE TABLE IF NOT EXISTS client_releases (
    version TEXT PRIMARY KEY,
    version_key INTEGER NOT NULL,          -- major * 1000000 + minor * 1000 + patch (ordering)
    platform TEXT NOT NULL DEFAULT 'windows',
    file_key TEXT NOT NULL,                -- R2 key
    file_name TEXT NOT NULL,
    size INTEGER NOT NULL,
    sha256 TEXT NOT NULL,
    notes TEXT NOT NULL DEFAULT '',
    published_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_client_releases_order ON client_releases (platform, version_key DESC);
