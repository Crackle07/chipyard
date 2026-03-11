-- Data collection schema for Chipyard builds and runs
-- Applied idempotently (CREATE IF NOT EXISTS)

CREATE TABLE IF NOT EXISTS builds (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    commit_hash TEXT NOT NULL,
    branch TEXT,
    remote_url TEXT,
    is_dirty INTEGER NOT NULL,
    config TEXT NOT NULL,
    build_path TEXT,
    user TEXT,
    host TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    build_id INTEGER,
    commit_hash TEXT NOT NULL,
    branch TEXT,
    remote_url TEXT,
    is_dirty INTEGER NOT NULL,
    config TEXT NOT NULL,
    benchmark TEXT,
    -- cycles INTEGER,
    -- instructions INTEGER,
    metrics TEXT,
    -- log_path TEXT,
    run_dir TEXT,
    simulator TEXT,
    github_user TEXT,
    user TEXT,
    host TEXT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (build_id) REFERENCES builds(id)
);

CREATE INDEX IF NOT EXISTS idx_builds_commit_config ON builds(commit_hash, config);
CREATE INDEX IF NOT EXISTS idx_runs_build_id ON runs(build_id);
CREATE INDEX IF NOT EXISTS idx_runs_config_benchmark ON runs(config, benchmark);
