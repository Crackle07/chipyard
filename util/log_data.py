#!/usr/bin/env python3
"""
Chipyard run logging: copy logs to shared storage and record runs in SQLite.
Never fails builds: on any error, print one warning and exit 0.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# Add util to path for datacoll imports
_SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPT_DIR))

from datacoll.config import resolve_config
from datacoll.db import apply_schema, get_or_create_build_id, init_db, insert_run


def _warn(msg: str) -> None:
    print(f"[log_data] Warning: {msg}", file=sys.stderr)


def _exit_ok() -> None:
    sys.exit(0)


def _exit_warn(msg: str) -> None:
    _warn(msg)
    _exit_ok()


def _get_chipyard_root(sim_dir: str | None) -> Path:
    """Infer Chipyard repo root from sim_dir or env."""
    if os.environ.get("CHIPYARD_ROOT"):
        return Path(os.environ["CHIPYARD_ROOT"])
    if sim_dir:
        # sim_dir is typically sims/verilator or sims/vcs
        p = Path(sim_dir).resolve()
        for _ in range(5):
            if (p / ".git").exists():
                return p
            p = p.parent
    return Path.cwd()


def should_collect(config: dict, datacoll_env: str | None) -> bool:
    """
    Tri-state: DATACOLL=on -> collect; DATACOLL=off -> no; unset -> use config.
    """
    if datacoll_env is not None and datacoll_env != "":
        return datacoll_env.lower() == "on"
    enabled = config.get("enable_data_collection", "on")
    if isinstance(enabled, bool):
        return enabled
    return str(enabled).lower() == "on"


def get_git_metadata(repo_root: Path) -> dict:
    """Return commit_hash, branch, remote_url, is_dirty. All empty on error."""
    out = {"commit_hash": "", "branch": "", "remote_url": "", "is_dirty": False}
    if not (repo_root / ".git").exists():
        return out
    try:
        out["commit_hash"] = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo_root, capture_output=True, text=True, timeout=5).stdout.strip() or ""
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    try:
        out["branch"] = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=repo_root, capture_output=True, text=True, timeout=5).stdout.strip() or ""
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    try:
        out["remote_url"] = subprocess.run(["git", "config", "--get", "remote.origin.url"], cwd=repo_root, capture_output=True, text=True, timeout=5).stdout.strip() or ""
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    try:
        r = subprocess.run(["git", "status", "--porcelain"], cwd=repo_root, capture_output=True, text=True, timeout=5)
        out["is_dirty"] = bool(r.stdout.strip()) if r.returncode == 0 else False
    except (subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return out


def parse_metrics(log_path: Path) -> dict:
    """Parse cycles, instructions, etc. from log. Returns dict for metrics JSON."""
    metrics: dict = {}
    if not log_path.exists():
        return metrics
    try:
        text = log_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return metrics
    # Common patterns in Chipyard/Verilator logs
    m = re.search(r"Cycle\s*:\s*(\d+)", text, re.I)
    if m:
        metrics["cycles"] = int(m.group(1))
    m = re.search(r"Inst\s*:\s*(\d+)", text, re.I)
    if m:
        metrics["instructions"] = int(m.group(1))
    m = re.search(r"(\d+)\s+cycles", text, re.I)
    if m and "cycles" not in metrics:
        metrics["cycles"] = int(m.group(1))
    return metrics


def parse_boom_perf_file(perf_path: Path) -> dict:
    """
    Parse the last BOOMV4_PERF_FINAL line from boom_perf.out.
    Returns a dict with all key=value pairs from that line.
    'instret' is mapped to 'instructions' for the top-level columns.
    """
    if not perf_path.exists():
        return {}
    try:
        text = perf_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    # Find the last BOOM_PERF_FINAL line (v3: BOOM_PERF_FINAL, v4: BOOMV4_PERF_FINAL)
    last_line = None
    for line in text.splitlines():
        if line.startswith("BOOM_PERF_FINAL") or line.startswith("BOOMV4_PERF_FINAL"):
            last_line = line
    if last_line is None:
        return {}
    result: dict = {}
    for kv in re.findall(r"(\w+)=(\d+)", last_line):
        key, val = kv
        result[key] = int(val)
    # Map instret -> instructions for top-level columns
    if "instret" in result:
        result["instructions"] = result.pop("instret")
    return result


def parse_github_user(remote_url: str | None) -> str:
    """Extract GitHub user/org from remote.origin.url (e.g. github.com/owner/repo -> owner)."""
    if not remote_url:
        return ""
    m = re.search(r"github\.com[/:]([^/]+)/", remote_url)
    return m.group(1) if m else ""


def run_init_db(config_path: str | None, chipyard_root: Path) -> None:
    """mkdir artifact_dir/{builds,logs}, create DB, apply schema."""
    try:
        cfg = resolve_config(config_path, chipyard_root)
        artifact_dir = Path(cfg.get("artifact_dir", "/scratch/chipyard-data"))
        build_dir = artifact_dir / cfg.get("build_dir", "builds")
        log_dir = artifact_dir / cfg.get("log_dir", "logs")
        db_path = cfg.get("db_path", str(artifact_dir / "chipyard.db"))

        build_dir.mkdir(parents=True, exist_ok=True)
        log_dir.mkdir(parents=True, exist_ok=True)

        conn = init_db(db_path)
        apply_schema(conn)
        conn.close()
    except Exception as e:
        _exit_warn(str(e))


def run_log(
    config_path: str | None,
    chipyard_config: str,
    bench: str,
    log_path: str,
    sim_dir: str | None,
    simulator: str | None,
    exit_code: int,
) -> None:
    """Copy log, insert run row. No-op if should_collect is False."""
    chipyard_root = _get_chipyard_root(sim_dir)
    try:
        cfg = resolve_config(config_path, chipyard_root)
    except Exception as e:
        _exit_warn(str(e))

    datacoll = os.environ.get("DATACOLL")
    if not should_collect(cfg, datacoll):
        _exit_ok()

    artifact_dir = Path(cfg.get("artifact_dir", "/scratch/chipyard-data"))
    log_subdir = artifact_dir / cfg.get("log_dir", "logs")
    db_path = cfg.get("db_path", str(artifact_dir / "chipyard.db"))

    src = Path(log_path)
    if not src.exists():
        _exit_warn(f"Log file not found: {log_path}")

    try:
        log_subdir.mkdir(parents=True, exist_ok=True)
    except OSError as e:
        _exit_warn(str(e))

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    user = os.environ.get("USER", "unknown")
    safe_bench = re.sub(r"[^\w\-.]", "_", bench)[:64]
    dst_name = f"{ts}_{user}_{chipyard_config}_{safe_bench}.log"
    dst = log_subdir / dst_name

    # try:
    #     shutil.copy2(src, dst)
    # except OSError as e:
    #     _exit_warn(str(e))

    git = get_git_metadata(chipyard_root)

    # If the repo is dirty, save git diff HEAD to a .diff file alongside the log.
    # Uncomment the block below and add a diff_path column to the schema/insert_run to use.
    #
    # diff_path = None
    # if git["is_dirty"]:
    #     try:
    #         diff_text = subprocess.run(
    #             ["git", "diff", "HEAD"], cwd=chipyard_root,
    #             capture_output=True, text=True, timeout=30
    #         ).stdout
    #         diff_file = log_subdir / dst_name.replace(".log", ".diff")
    #         diff_file.write_text(diff_text, encoding="utf-8")
    #         diff_path = str(diff_file)
    #     except Exception:
    #         pass

    metrics_dict = parse_metrics(src)

    # Auto-detect boom_perf.out from sim_dir and merge BOOM perf counters
    if sim_dir:
        boom_perf_path = Path(sim_dir) / "output" / "boom_perf.out"
        boom_perf = parse_boom_perf_file(boom_perf_path)
        if boom_perf:
            for key in ("cycles", "instructions"):
                if key in boom_perf:
                    metrics_dict[key] = boom_perf.pop(key)
            perf_counters = boom_perf
        else:
            perf_counters = None

    cycles = metrics_dict.get("cycles")
    instructions = metrics_dict.get("instructions")
    ipc = None
    cpi = None
    if cycles and instructions and cycles > 0:
        ipc = instructions / cycles
        cpi = cycles / instructions
        metrics_dict["ipc"] = ipc
        metrics_dict["cpi"] = cpi
    if perf_counters:
        metrics_dict["performance_counters"] = perf_counters
    metrics_json = json.dumps(metrics_dict)

    # Ensure DB exists
    try:
        Path(db_path).parent.mkdir(parents=True, exist_ok=True)
        conn = init_db(db_path)
        try:
            apply_schema(conn)
        except Exception:
            pass
        conn.close()
        conn = init_db(db_path)
    except Exception as e:
        _exit_warn(str(e))

    build_id = None
    try:
        build_id = get_or_create_build_id(
            conn,
            commit_hash=git["commit_hash"] or "unknown",
            config=chipyard_config,
            branch=git["branch"],
            remote_url=git["remote_url"],
            is_dirty=git["is_dirty"],
            build_path=None,
            user=os.environ.get("USER"),
            host=os.environ.get("HOSTNAME") or os.environ.get("HOST", ""),
        )
    except Exception:
        pass

    # log_path_stored = str(dst.resolve())
    run_dir_stored = str(Path(sim_dir).resolve()) if sim_dir else ""
    github_user = parse_github_user(git["remote_url"])

    try:
        insert_run(
            conn,
            build_id=build_id,
            commit_hash=git["commit_hash"] or "unknown",
            branch=git["branch"],
            remote_url=git["remote_url"],
            is_dirty=git["is_dirty"],
            config=chipyard_config,
            benchmark=bench,
            # cycles=cycles,
            # instructions=instructions,
            metrics=metrics_json,
            # log_path=log_path_stored,
            run_dir=run_dir_stored,
            simulator=simulator,
            github_user=github_user,
            user=os.environ.get("USER"),
            host=os.environ.get("HOSTNAME") or os.environ.get("HOST", ""),
        )
    except Exception as e:
        _exit_warn(str(e))
    finally:
        conn.close()

    _exit_ok()


def main() -> None:
    ap = argparse.ArgumentParser(description="Chipyard data collection: init DB or log a run")
    ap.add_argument("--init-db", action="store_true", help="Initialize database and directories")
    ap.add_argument("--config-file", metavar="PATH", default=None, help="Path to .chipyard/config.json")
    ap.add_argument("--chipyard-config", metavar="N", default="", help="Chipyard config name (e.g. RocketConfig)")
    ap.add_argument("--bench", metavar="BENCHMARK", default="", help="Benchmark/binary name")
    ap.add_argument("--log-path", metavar="PATH", default="", help="Path to simulation log file")
    ap.add_argument("--sim-dir", metavar="PATH", default=None, help="Simulator directory (for repo context)")
    ap.add_argument("--simulator", metavar="NAME", default="", help="Simulator name (e.g. verilator, vcs)")
    ap.add_argument("--exit-code", type=int, default=0, help="Simulation exit code (for future use)")
    args = ap.parse_args()

    chipyard_root = _get_chipyard_root(args.sim_dir)

    if args.init_db:
        run_init_db(args.config_file, chipyard_root)
        return

    # Run logging path
    if not args.chipyard_config or not args.bench or not args.log_path:
        _exit_ok()

    run_log(
        config_path=args.config_file,
        chipyard_config=args.chipyard_config,
        bench=args.bench,
        log_path=args.log_path,
        sim_dir=args.sim_dir,
        simulator=args.simulator or None,
        exit_code=args.exit_code,
    )


if __name__ == "__main__":
    main()
