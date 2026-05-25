#!/usr/bin/env python3
"""Compare SSD latency CSV baselines against a new run.

The tool accepts either two CSV files or two directories. Directory mode
compares every CSV found under the baseline directory with the same relative
path under the new directory. Numeric columns are tracked; metadata columns
such as dates, source labels, and notes are ignored.
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path


IGNORED_COLUMNS = {"date", "source", "benchmark", "notes"}


def is_number(value: str) -> bool:
    try:
        float(value)
        return True
    except ValueError:
        return False


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def relative_delta(old: float, new: float) -> float:
    if old == 0.0:
        return 0.0 if new == 0.0 else math.inf
    return abs(new - old) / abs(old)


def comparable_files(baseline: Path, new: Path) -> list[tuple[Path, Path, str]]:
    if baseline.is_file() and new.is_file():
        return [(baseline, new, baseline.name)]
    if baseline.is_dir() and new.is_dir():
        files: list[tuple[Path, Path, str]] = []
        for base_file in sorted(baseline.rglob("*.csv")):
            rel = base_file.relative_to(baseline)
            new_file = new / rel
            if not new_file.exists():
                raise FileNotFoundError(f"missing new CSV for {rel}")
            files.append((base_file, new_file, str(rel)))
        return files
    raise ValueError("baseline and new paths must both be files or both be directories")


def tracked_columns(rows: list[dict[str, str]]) -> list[str]:
    if not rows:
        return []
    columns: list[str] = []
    for column in rows[0]:
        if column in IGNORED_COLUMNS:
            continue
        values = [row.get(column, "") for row in rows]
        if values and all(value != "" and is_number(value) for value in values):
            columns.append(column)
    return columns


def compare_one(name: str,
                baseline_rows: list[dict[str, str]],
                new_rows: list[dict[str, str]],
                threshold: float) -> list[tuple[str, int, str, float, float, float, bool]]:
    if len(baseline_rows) != len(new_rows):
        raise ValueError(
            f"{name}: row count differs: baseline={len(baseline_rows)} new={len(new_rows)}")

    columns = tracked_columns(baseline_rows)
    results: list[tuple[str, int, str, float, float, float, bool]] = []
    for row_idx, (base_row, new_row) in enumerate(zip(baseline_rows, new_rows), start=1):
        for column in columns:
            new_value = new_row.get(column, "")
            if new_value == "" or not is_number(new_value):
                raise ValueError(f"{name}: row {row_idx} column {column} is not numeric")
            old = float(base_row[column])
            new = float(new_value)
            delta = relative_delta(old, new)
            failed = delta > threshold
            results.append((name, row_idx, column, old, new, delta, failed))
    return results


def print_summary(results: list[tuple[str, int, str, float, float, float, bool]],
                  threshold: float,
                  verbose: bool) -> None:
    print(f"CSV regression threshold: {threshold * 100:.2f}%")
    by_file: dict[str, list[tuple[str, int, str, float, float, float, bool]]] = {}
    for result in results:
        by_file.setdefault(result[0], []).append(result)

    print("file,tracked_cells,max_delta_pct,failures,status")
    for name in sorted(by_file):
        file_results = by_file[name]
        max_delta = max((result[5] for result in file_results), default=0.0)
        failures = sum(1 for result in file_results if result[6])
        delta_text = "inf" if math.isinf(max_delta) else f"{max_delta * 100.0:.3f}"
        status = "FAIL" if failures else "ok"
        print(f"{name},{len(file_results)},{delta_text},{failures},{status}")

    failing = [result for result in results if result[6]]
    if verbose or failing:
        print("file,row,column,baseline,new,delta_pct,status")
        for name, row, column, old, new, delta, failed in results:
            if not verbose and not failed:
                continue
            delta_pct = math.inf if math.isinf(delta) else delta * 100.0
            status = "FAIL" if failed else "ok"
            delta_text = "inf" if math.isinf(delta_pct) else f"{delta_pct:.3f}"
            print(f"{name},{row},{column},{old:.12g},{new:.12g},{delta_text},{status}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("new", type=Path)
    parser.add_argument("--threshold", type=float, default=0.05,
                        help="relative movement threshold, default 0.05")
    parser.add_argument("--verbose", action="store_true",
                        help="print every tracked cell, not just per-file summary/failures")
    args = parser.parse_args(argv)

    all_results: list[tuple[str, int, str, float, float, float, bool]] = []
    try:
        for base_file, new_file, name in comparable_files(args.baseline, args.new):
            all_results.extend(
                compare_one(name, read_csv(base_file), read_csv(new_file), args.threshold))
    except (FileNotFoundError, ValueError) as e:
        print(f"compare_csvs.py: {e}", file=sys.stderr)
        return 2

    print_summary(all_results, args.threshold, args.verbose)
    return 1 if any(result[-1] for result in all_results) else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
