"""DB init, schema creation, insert_build, insert_run, get_or_create_build_id."""

from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Any

_SCHEMA_PATH = Path(__file__).resolve().parent / "schema.sql"


def init_db(db_path: str | Path) -> sqlite3.Connection:
    """Create DB file and directory if missing. Does not apply schema."""
    path = Path(db_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    return sqlite3.connect(str(path))


def apply_schema(conn: sqlite3.Connection) -> None:
    """Apply schema.sql idempotently."""
    sql = _SCHEMA_PATH.read_text(encoding="utf-8")
    conn.executescript(sql)
    conn.commit()
    _migrate_add_columns(conn)


def _migrate_add_columns(conn: sqlite3.Connection) -> None:
    """Add new columns to existing runs table if missing."""
    for col, ctype in [("run_dir", "TEXT"), ("simulator", "TEXT"), ("github_user", "TEXT")]:
        try:
            conn.execute(f"ALTER TABLE runs ADD COLUMN {col} {ctype}")
            conn.commit()
        except sqlite3.OperationalError as e:
            if "duplicate column" not in str(e).lower():
                raise


def get_or_create_build_id(
    conn: sqlite3.Connection,
    commit_hash: str,
    config: str,
    branch: str | None,
    remote_url: str | None,
    is_dirty: bool,
    build_path: str | None,
    user: str | None,
    host: str | None,
) -> int | None:
    """Get existing build id or insert new row. Returns None if builds table not used."""
    cur = conn.execute(
        "SELECT id FROM builds WHERE commit_hash=? AND config=?",
        (commit_hash, config),
    )
    row = cur.fetchone()
    if row:
        return row[0]
    cur = conn.execute(
        """INSERT INTO builds (commit_hash, branch, remote_url, is_dirty, config, build_path, user, host)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
        (
            commit_hash,
            branch or "",
            remote_url or "",
            1 if is_dirty else 0,
            config,
            build_path or "",
            user or "",
            host or "",
        ),
    )
    conn.commit()
    return cur.lastrowid


def insert_run(
    conn: sqlite3.Connection,
    *,
    build_id: int | None = None,
    commit_hash: str,
    branch: str | None,
    remote_url: str | None,
    is_dirty: bool,
    config: str,
    benchmark: str | None,
    # cycles: int | None = None,
    # instructions: int | None = None,
    metrics: str = "{}",
    # log_path: str | None,
    run_dir: str | None = None,
    simulator: str | None = None,
    github_user: str | None = None,
    user: str | None,
    host: str | None,
) -> None:
    """Insert a row into runs."""
    conn.execute(
        """INSERT INTO runs (
            build_id, commit_hash, branch, remote_url, is_dirty, config,
            benchmark, metrics, run_dir, simulator, github_user, user, host
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (
            build_id,
            commit_hash,
            branch or "",
            remote_url or "",
            1 if is_dirty else 0,
            config,
            benchmark or "",
            metrics,
            run_dir or "",
            simulator or "",
            github_user or "",
            user or "",
            host or "",
        ),
    )
    conn.commit()
