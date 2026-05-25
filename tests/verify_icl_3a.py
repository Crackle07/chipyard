#!/usr/bin/env python3
"""Check Phase 3a ICL benchmarks against the bounded-WB timing model and an
explicit LRU state machine.

Two checks per benchmark:
  - timing: latency cycles agree with the analytical model (existing).
  - state-machine: per-write was_hit / was_eviction_triggered / victim_lpn
    flags in icl_csv_write rows match a hand-predicted LRU state machine,
    byte-identical.

The state-machine check requires icl_csv_write rows in the run log; these are
emitted by blockdev_t::schedule_write_ack(). Logs captured before that change
report state-machine: SKIP.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
import tempfile
from pathlib import Path
from typing import Optional


GLOBAL_SECTION = "ssd"
SECTOR_BYTES = 512
BRIDGE_BEAT_BYTES = 32
PER_REQUEST_DRAIN_OVERHEAD_CYCLES = 103.589285714
CYCLES_PER_MMIO_BEAT = 7.776785714

# The write benchmarks bracket blkdev_send_request() through write-ack
# observation. That includes a fixed target-side request/ack path cost that is
# not present in the read-response drain fit used by verify_pm1725a_metasim.py.
# Fit from the Phase 3a metasim fill baseline on Crackle07:
#   measured - (bridge upload + model ack/writeback) ~= 2176 cycles
# across the fast fill writes and the LSB dirty-eviction writes.
WRITE_REQUEST_PATH_OVERHEAD_CYCLES = 2176

ICL_CSV_RE = re.compile(
    r"^icl_csv_write\s+seq=(\d+)\s+tag=(\d+)\s+offset=(\d+)\s+len=(\d+)\s+"
    r"lpn=(\d+)\s+t_submit=(\d+)\s+t_ack=(\d+)\s+latency_cycles=(\d+)\s+"
    r"was_hit=(\d+)\s+was_eviction_triggered=(\d+)\s+victim_lpn=(-?\d+)"
    r"(?:\s+icl_rmw_triggered=(\d+)\s+icl_rmw_fetch_cycles=(\d+))?\s*$"
)
ICL_CSV_READ_RE = re.compile(
    r"^icl_csv_read\s+seq=(\d+)\s+tag=(\d+)\s+offset=(\d+)\s+len=(\d+)\s+"
    r"lpn=(\d+)\s+t_submit=(\d+)\s+t_ack=(\d+)\s+latency_cycles=(\d+)\s+"
    r"was_read_hit=(\d+)\s*$"
)


def parse_ini(path: Path) -> dict[str, dict[str, str]]:
    ini: dict[str, dict[str, str]] = {}
    section = GLOBAL_SECTION
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
            ini.setdefault(section, {})
            continue
        key, value = line.split("=", 1)
        ini.setdefault(section, {})[key.strip().lower()] = value.strip()
    return ini


def find_value(ini: dict[str, dict[str, str]], keys: list[tuple[str, str]]) -> str | None:
    for section, key in keys:
        value = ini.get(section, {}).get(key)
        if value is not None:
            return value
    return None


def get_int(ini: dict[str, dict[str, str]], keys: list[tuple[str, str]], default: int) -> int:
    value = find_value(ini, keys)
    return int(value, 0) if value is not None else default


def get_float(ini: dict[str, dict[str, str]], keys: list[tuple[str, str]], default: float) -> float:
    value = find_value(ini, keys)
    return float(value) if value is not None else default


def get_str(ini: dict[str, dict[str, str]], keys: list[tuple[str, str]], default: str) -> str:
    value = find_value(ini, keys)
    return value.strip() if value is not None else default


def ns_to_cycles(ns: int, target_clock_hz: int) -> int:
    return int(math.ceil(ns * target_clock_hz / 1_000_000_000))


def transfer_cycles(bytes_: int, bytes_per_sec: float, target_clock_hz: int) -> int:
    if bytes_ == 0:
        return 0
    return max(1, int(math.ceil(bytes_ * target_clock_hz / bytes_per_sec)))


def page_type_for_lpn(lpn: int, cfg: dict[str, int | str]) -> str:
    if cfg["cell_type"] == "SLC":
        return "SINGLE"
    addr_in_block = lpn % int(cfg["pages_per_block"])
    if addr_in_block < int(cfg["n_meta_pages"]):
        return "LSB"
    f = ((addr_in_block - int(cfg["n_meta_pages"])) // int(cfg["n_planes"])) % int(
        cfg["bits_per_cell"]
    )
    if f == 0:
        return "LSB"
    if f == 1:
        return "CSB"
    return "MSB"


def load_cfg(ini: dict[str, dict[str, str]]) -> dict[str, int | str | float]:
    target_clock_hz = get_int(
        ini,
        [("sim", "targetclockhz"), (GLOBAL_SECTION, "target_clock_hz"),
         (GLOBAL_SECTION, "targetclockhz")],
        1_000_000_000,
    )
    cell_type = get_str(
        ini,
        [(GLOBAL_SECTION, "cell_type"), (GLOBAL_SECTION, "nandtype"),
         ("pal", "nandtype")],
        "SLC",
    ).upper()
    page_size_bytes = get_int(
        ini,
        [("pal", "pagesize"), (GLOBAL_SECTION, "page_size_bytes"),
         (GLOBAL_SECTION, "pagesize")],
        16384 if cell_type == "TLC" else 4096,
    )
    bus_speed_mtps = get_int(
        ini,
        [("pal", "busspeedmtps"), (GLOBAL_SECTION, "bus_speed_mtps"),
         (GLOBAL_SECTION, "busspeedmtps")],
        533 if cell_type == "TLC" else 0,
    )
    dma_bytes_per_sec = float(bus_speed_mtps * 1_000_000) if bus_speed_mtps else 1_000_000_000.0
    dma_bytes_per_sec = get_float(
        ini,
        [("pal", "dmaspeed"), (GLOBAL_SECTION, "dma_speed_bps"),
         (GLOBAL_SECTION, "dma_bytes_per_sec")],
        dma_bytes_per_sec,
    )
    bits_per_cell = get_int(
        ini,
        [("pal", "bitspercell"), (GLOBAL_SECTION, "bits_per_cell"),
         (GLOBAL_SECTION, "bitspercell")],
        {"SLC": 1, "MLC": 2, "TLC": 3, "QLC": 4}[cell_type],
    )
    n_planes = get_int(
        ini,
        [("pal", "plane"), (GLOBAL_SECTION, "n_planes"), (GLOBAL_SECTION, "planes")],
        1,
    )
    pages_per_block = get_int(
        ini,
        [("pal", "pagesperblock"), (GLOBAL_SECTION, "pages_per_block"),
         (GLOBAL_SECTION, "pagesperblock")],
        1024,
    )
    n_meta_pages = get_int(
        ini,
        [("pal", "nmetapages"), (GLOBAL_SECTION, "n_meta_pages"),
         (GLOBAL_SECTION, "nmetapages")],
        8,
    )

    def latency(keys: list[tuple[str, str]], default_ns: int) -> int:
        value = find_value(ini, keys)
        return ns_to_cycles(int(value, 0) if value is not None else default_ns, target_clock_hz)

    read_lsb = latency(
        [("pal", "read.lsb"), (GLOBAL_SECTION, "tr_lsb_ns"),
         (GLOBAL_SECTION, "read_lsb_ns")],
        77000 if cell_type == "TLC" else 1,
    )
    read_csb = latency(
        [("pal", "read.csb"), (GLOBAL_SECTION, "tr_csb_ns"),
         (GLOBAL_SECTION, "read_csb_ns")],
        120000 if cell_type == "TLC" else 1,
    )
    read_msb = latency(
        [("pal", "read.msb"), (GLOBAL_SECTION, "tr_msb_ns"),
         (GLOBAL_SECTION, "read_msb_ns")],
        140000 if cell_type == "TLC" else 1,
    )
    if cell_type == "SLC":
        read_csb = read_lsb
        read_msb = read_lsb
    program_lsb = latency(
        [("pal", "program.lsb"), (GLOBAL_SECTION, "tprog_lsb_ns"),
         (GLOBAL_SECTION, "program_lsb_ns")],
        700000 if cell_type == "TLC" else 1,
    )
    program_csb = latency(
        [("pal", "program.csb"), (GLOBAL_SECTION, "tprog_csb_ns"),
         (GLOBAL_SECTION, "program_csb_ns")],
        1300000 if cell_type == "TLC" else 1,
    )
    program_msb = latency(
        [("pal", "program.msb"), (GLOBAL_SECTION, "tprog_msb_ns"),
         (GLOBAL_SECTION, "program_msb_ns")],
        2500000 if cell_type == "TLC" else 1,
    )
    if cell_type == "SLC":
        program_csb = program_lsb
        program_msb = program_lsb

    return {
        "target_clock_hz": target_clock_hz,
        "cell_type": cell_type,
        "bits_per_cell": bits_per_cell,
        "n_channels": get_int(
            ini,
            [("pal", "channel"), (GLOBAL_SECTION, "n_channels"),
             (GLOBAL_SECTION, "channels")],
            1,
        ),
        "n_ways": get_int(
            ini,
            [("pal", "way"), (GLOBAL_SECTION, "n_ways"), (GLOBAL_SECTION, "ways")],
            1,
        ),
        "n_dies_per_channel": get_int(
            ini,
            [("pal", "die"), (GLOBAL_SECTION, "n_dies_per_channel"),
             (GLOBAL_SECTION, "n_dies"), (GLOBAL_SECTION, "dies")],
            1,
        ),
        "n_planes": n_planes,
        "pages_per_block": pages_per_block,
        "n_meta_pages": n_meta_pages,
        "page_size_bytes": page_size_bytes,
        "dma_bytes_per_sec": dma_bytes_per_sec,
        "wb_ack_latency_cycles": get_int(
            ini,
            [("hil", "wbacklatencycycles"),
             (GLOBAL_SECTION, "wb_ack_latency_cycles"),
             (GLOBAL_SECTION, "wbacklatencycycles")],
            0,
        ),
        "host_overhead_cycles": get_int(
            ini,
            [("hil", "hostoverheadcycles"),
             (GLOBAL_SECTION, "host_overhead_cycles"),
             (GLOBAL_SECTION, "hostoverheadcycles")],
            0,
        ),
        "icl_capacity_bytes": get_int(
            ini,
            [("hil", "iclcapacitybytes"), (GLOBAL_SECTION, "icl_capacity_bytes"),
             (GLOBAL_SECTION, "iclcapacitybytes")],
            page_size_bytes,
        ),
        "program_lsb_cycles": program_lsb,
        "program_csb_cycles": program_csb,
        "program_msb_cycles": program_msb,
        "read_lsb_cycles": read_lsb,
        "read_csb_cycles": read_csb,
        "read_msb_cycles": read_msb,
        "cmd_overhead_cycles": get_int(
            ini,
            [("hil", "cmdoverhead"), (GLOBAL_SECTION, "cmd_overhead_cycles"),
             (GLOBAL_SECTION, "cmdoverhead")],
            0,
        ),
        "completion_overhead_cycles": get_int(
            ini,
            [("hil", "completionoverhead"),
             (GLOBAL_SECTION, "completion_overhead_cycles"),
             (GLOBAL_SECTION, "completionoverhead")],
            0,
        ),
        "host_bandwidth_bps": get_float(
            ini,
            [("hil", "hostbandwidth"), (GLOBAL_SECTION, "host_bandwidth_bps"),
             (GLOBAL_SECTION, "hostbandwidth")],
            1_000_000_000.0,
        ),
    }


def program_cycles(page_type: str, cfg: dict[str, int | str | float]) -> int:
    if page_type in ("SINGLE", "LSB"):
        return int(cfg["program_lsb_cycles"])
    if page_type == "CSB":
        return int(cfg["program_csb_cycles"])
    return int(cfg["program_msb_cycles"])


def read_cycles_for_page_type(page_type: str, cfg: dict[str, int | str | float]) -> int:
    if page_type in ("SINGLE", "LSB"):
        return int(cfg["read_lsb_cycles"])
    if page_type == "CSB":
        return int(cfg["read_csb_cycles"])
    return int(cfg["read_msb_cycles"])


def predict_read_miss_model_latency(lpn: int, cfg: dict[str, int | str | float]) -> int:
    """Model-side (t_ack - t_submit) prediction for a PAL read of one LPN.

    Mirrors ssd_latency_model.cc::submit_timing's read path:
      t_done = t_arrival + cmd_overhead + schedule_page_op(read, page_bytes)
      host_start = reserve_host(t_done + completion_overhead, host_transfer)
      host_complete = host_start + host_transfer + host_overhead
    For a single, cold read the host bandwidth resource is idle so
    host_start = t_done + completion_overhead.
    """
    page_bytes = int(cfg["page_size_bytes"])
    target_clock_hz = int(cfg["target_clock_hz"])
    nand_dma = transfer_cycles(page_bytes, float(cfg["dma_bytes_per_sec"]), target_clock_hz)
    host_transfer = transfer_cycles(page_bytes, float(cfg["host_bandwidth_bps"]), target_clock_hz)
    page_type = page_type_for_lpn(lpn, cfg)
    read_lat = read_cycles_for_page_type(page_type, cfg)
    pre_dma = 1
    return (
        int(cfg["cmd_overhead_cycles"])
        + pre_dma
        + read_lat
        + nand_dma
        + int(cfg["completion_overhead_cycles"])
        + host_transfer
        + int(cfg["host_overhead_cycles"])
    )


class PalState:
    """Small mirror of SsdLatencyModel::schedule_page_op for write verifiers."""

    def __init__(self, cfg: dict[str, int | str | float]) -> None:
        self.cfg = cfg
        self.channel_reservations: list[list[tuple[int, int]]] = [
            [] for _ in range(int(cfg["n_channels"]))
        ]
        n_dies = (
            int(cfg["n_channels"])
            * int(cfg["n_ways"])
            * int(cfg["n_dies_per_channel"])
            * int(cfg["n_planes"])
        )
        self.die_busy_until = [0 for _ in range(n_dies)]

    def map_lpn(self, lpn: int) -> tuple[int, int, int, int]:
        channel = lpn % int(self.cfg["n_channels"])
        lpn //= int(self.cfg["n_channels"])
        way = lpn % int(self.cfg["n_ways"])
        lpn //= int(self.cfg["n_ways"])
        die = lpn % int(self.cfg["n_dies_per_channel"])
        lpn //= int(self.cfg["n_dies_per_channel"])
        plane = lpn % int(self.cfg["n_planes"])
        return channel, way, die, plane

    def die_index(self, addr: tuple[int, int, int, int]) -> int:
        channel, way, die, plane = addr
        return (
            ((channel * int(self.cfg["n_ways"]) + way)
             * int(self.cfg["n_dies_per_channel"]) + die)
            * int(self.cfg["n_planes"])
            + plane
        )

    def reserve_channel(self, channel: int, earliest: int, cycles: int) -> int:
        if cycles == 0:
            return earliest
        reservations = self.channel_reservations[channel]
        start = earliest
        insert_at = len(reservations)
        for idx, (busy_start, busy_end) in enumerate(reservations):
            if start + cycles <= busy_start:
                insert_at = idx
                break
            if start < busy_end:
                start = busy_end
        reservations.insert(insert_at, (start, start + cycles))
        return start

    def schedule_page_op(self, lpn: int, write: bool, bytes_: int, t_arrival: int) -> int:
        addr = self.map_lpn(lpn)
        die_idx = self.die_index(addr)
        page_type = page_type_for_lpn(lpn, self.cfg)
        pre_dma = (
            transfer_cycles(bytes_, float(self.cfg["dma_bytes_per_sec"]),
                            int(self.cfg["target_clock_hz"]))
            if write else 1
        )
        mem_op = program_cycles(page_type, self.cfg) if write else read_cycles_for_page_type(
            page_type, self.cfg
        )
        post_dma = (
            1 if write else transfer_cycles(bytes_, float(self.cfg["dma_bytes_per_sec"]),
                                            int(self.cfg["target_clock_hz"]))
        )

        t_pre_start = self.reserve_channel(
            addr[0], max(t_arrival, self.die_busy_until[die_idx]), pre_dma
        )
        t_pre_end = t_pre_start + pre_dma
        t_mem_end = t_pre_end + mem_op
        t_post_start = self.reserve_channel(addr[0], t_mem_end, post_dma)
        t_post_end = t_post_start + post_dma
        self.die_busy_until[die_idx] = t_mem_end
        return t_post_end


class LruStateMachine:
    """Hand-predicted LRU write-buffer state.

    See ssd_latency_model.cc::submit_write_buffered and submit_read_cache_hit.
    cached_lpns is MRU-front, LRU-back, matching std::list lru push_front /
    pop_back.

    Phase 3b semantics: writes install lines (with eviction at capacity).
    Reads do NOT install — a read miss leaves cached_lpns unchanged. Reads
    that hit move the LPN to the MRU front.
    """

    def __init__(self, capacity_lines: int) -> None:
        if capacity_lines <= 0:
            raise ValueError(f"capacity_lines must be positive, got {capacity_lines}")
        self.capacity_lines = capacity_lines
        self.cached_lpns: list[int] = []

    def step_write(self, lpn: int) -> tuple[bool, bool, Optional[int]]:
        if lpn in self.cached_lpns:
            self.cached_lpns.remove(lpn)
            self.cached_lpns.insert(0, lpn)
            return True, False, None
        if len(self.cached_lpns) < self.capacity_lines:
            self.cached_lpns.insert(0, lpn)
            return False, False, None
        victim = self.cached_lpns.pop()
        self.cached_lpns.insert(0, lpn)
        return False, True, victim

    def step_subpage_write(self, lpn: int) -> tuple[bool, bool, bool, Optional[int]]:
        hit, eviction, victim = self.step_write(lpn)
        return hit, eviction, not hit, victim

    def step_read(self, lpn: int) -> bool:
        if lpn in self.cached_lpns:
            self.cached_lpns.remove(lpn)
            self.cached_lpns.insert(0, lpn)
            return True
        return False


def parse_fill_log(path: Path) -> list[tuple[int, int]]:
    pattern = re.compile(r"^icl_fill_write\s+page\s+(\d+)\s+cycles\s+(\d+)\s*$")
    rows: list[tuple[int, int]] = []
    for line in path.read_text(errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            rows.append((int(match.group(1)), int(match.group(2))))
    return rows


def parse_icl_csv_rows(path: Path) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in path.read_text(errors="replace").splitlines():
        match = ICL_CSV_RE.match(line)
        if not match:
            continue
        rows.append({
            "kind": "write",
            "seq": int(match.group(1)),
            "tag": int(match.group(2)),
            "offset": int(match.group(3)),
            "len": int(match.group(4)),
            "lpn": int(match.group(5)),
            "t_submit": int(match.group(6)),
            "t_ack": int(match.group(7)),
            "latency_cycles": int(match.group(8)),
            "was_hit": int(match.group(9)),
            "was_eviction_triggered": int(match.group(10)),
            "victim_lpn": int(match.group(11)),
            "has_rmw_fields": 1 if match.group(12) is not None else 0,
            "icl_rmw_triggered": int(match.group(12) or 0),
            "icl_rmw_fetch_cycles": int(match.group(13) or 0),
        })
    rows.sort(key=lambda r: r["seq"])
    return rows


def parse_icl_csv_read_rows(path: Path) -> list[dict[str, int]]:
    rows: list[dict[str, int]] = []
    for line in path.read_text(errors="replace").splitlines():
        match = ICL_CSV_READ_RE.match(line)
        if not match:
            continue
        rows.append({
            "kind": "read",
            "seq": int(match.group(1)),
            "tag": int(match.group(2)),
            "offset": int(match.group(3)),
            "len": int(match.group(4)),
            "lpn": int(match.group(5)),
            "t_submit": int(match.group(6)),
            "t_ack": int(match.group(7)),
            "latency_cycles": int(match.group(8)),
            "was_read_hit": int(match.group(9)),
        })
    rows.sort(key=lambda r: r["seq"])
    return rows


def merge_csv_events(
    writes: list[dict[str, int]],
    reads: list[dict[str, int]],
) -> list[dict[str, int]]:
    """Order writes and reads by t_submit. seq breaks ties within each kind;
    when t_submit ties across kinds, writes precede reads (writes ack from
    the data-arrival path, reads issue immediately on request)."""
    events = list(writes) + list(reads)
    kind_order = {"write": 0, "read": 1}
    events.sort(key=lambda e: (e["t_submit"], kind_order[e["kind"]], e["seq"]))
    return events


def is_full_page_write(ev: dict[str, int], cfg: dict[str, int | str | float]) -> bool:
    page_bytes = int(cfg["page_size_bytes"])
    start_byte = ev["offset"] * SECTOR_BYTES
    bytes_ = ev["len"] * SECTOR_BYTES
    return bytes_ == page_bytes and (start_byte % page_bytes) == 0


def predict_fill_page(page: int, cfg: dict[str, int | str | float]) -> tuple[int, int]:
    line_bytes = int(cfg["page_size_bytes"])
    capacity_lines = int(cfg["icl_capacity_bytes"]) // line_bytes
    beats = line_bytes // BRIDGE_BEAT_BYTES
    target_upload = round(PER_REQUEST_DRAIN_OVERHEAD_CYCLES + CYCLES_PER_MMIO_BEAT * beats)
    fast_ack = int(cfg["wb_ack_latency_cycles"]) + int(cfg["host_overhead_cycles"])
    if page < capacity_lines:
        return target_upload + fast_ack + WRITE_REQUEST_PATH_OVERHEAD_CYCLES, 60

    victim_lpn = page - capacity_lines
    page_type = page_type_for_lpn(victim_lpn, cfg)  # LRU victim for serialized fill.
    writeback = (
        transfer_cycles(line_bytes, float(cfg["dma_bytes_per_sec"]), int(cfg["target_clock_hz"]))
        + program_cycles(page_type, cfg)
        + 1
    )
    return target_upload + max(fast_ack, writeback) + WRITE_REQUEST_PATH_OVERHEAD_CYCLES, 200


def fill_timing_check(log_path: Path, cfg: dict[str, int | str | float]) -> tuple[str, list[str]]:
    rows = parse_fill_log(log_path)
    if not rows:
        return "SKIP", ["no icl_fill_write rows in log"]
    failures: list[str] = []
    for page, measured in rows:
        predicted, tolerance = predict_fill_page(page, cfg)
        delta = measured - predicted
        if abs(delta) > tolerance:
            failures.append(
                f"page {page}: measured={measured} predicted={predicted} "
                f"delta={delta} tolerance={tolerance}"
            )
    return ("PASS" if not failures else "FAIL"), failures


def state_machine_check(
    events: list[dict[str, int]],
    capacity_lines: int,
    cfg: dict[str, int | str | float],
) -> tuple[str, list[str]]:
    if not events:
        return "SKIP", ["no icl_csv_write/icl_csv_read rows in log (regenerate baseline)"]
    sm = LruStateMachine(capacity_lines)
    failures: list[str] = []
    for ev in events:
        if ev["kind"] == "write":
            if is_full_page_write(ev, cfg):
                pred_hit, pred_evict, pred_victim = sm.step_write(ev["lpn"])
            else:
                pred_hit, pred_evict, _, pred_victim = sm.step_subpage_write(
                    ev["lpn"]
                )
            measured_hit = bool(ev["was_hit"])
            measured_evict = bool(ev["was_eviction_triggered"])
            measured_victim = ev["victim_lpn"] if ev["victim_lpn"] >= 0 else None
            if (pred_hit, pred_evict, pred_victim) != (
                measured_hit,
                measured_evict,
                measured_victim,
            ):
                failures.append(
                    f"write seq {ev['seq']} lpn {ev['lpn']}: predicted "
                    f"hit={pred_hit} evict={pred_evict} victim={pred_victim} "
                    f"-- measured hit={measured_hit} evict={measured_evict} "
                    f"victim={measured_victim}"
                )
        else:
            pred_hit = sm.step_read(ev["lpn"])
            measured_hit = bool(ev["was_read_hit"])
            if pred_hit != measured_hit:
                failures.append(
                    f"read seq {ev['seq']} lpn {ev['lpn']}: predicted "
                    f"hit={pred_hit} -- measured hit={measured_hit}"
                )
    return ("PASS" if not failures else "FAIL"), failures


def rmw_flag_check(
    events: list[dict[str, int]],
    capacity_lines: int,
    cfg: dict[str, int | str | float],
) -> tuple[str, list[str]]:
    writes = [ev for ev in events if ev["kind"] == "write"]
    if not writes:
        return "SKIP", ["no icl_csv_write rows in log"]
    if not any(ev.get("has_rmw_fields", 0) for ev in writes):
        return "SKIP", ["icl_csv_write rows have no RMW fields (regenerate baseline)"]

    sm = LruStateMachine(capacity_lines)
    failures: list[str] = []
    for ev in events:
        if ev["kind"] == "read":
            sm.step_read(ev["lpn"])
            continue
        full_page = is_full_page_write(ev, cfg)
        if full_page:
            pred_hit, pred_evict, pred_victim = sm.step_write(ev["lpn"])
            pred_rmw = False
        else:
            pred_hit, pred_evict, pred_rmw, pred_victim = sm.step_subpage_write(
                ev["lpn"]
            )
        del pred_hit, pred_evict, pred_victim
        measured_rmw = bool(ev["icl_rmw_triggered"])
        if pred_rmw != measured_rmw:
            failures.append(
                f"write seq {ev['seq']} lpn {ev['lpn']}: predicted "
                f"rmw={pred_rmw} -- measured rmw={measured_rmw}"
            )
        if not pred_rmw and ev["icl_rmw_fetch_cycles"] != 0:
            failures.append(
                f"write seq {ev['seq']} lpn {ev['lpn']}: predicted no RMW "
                f"but fetch_cycles={ev['icl_rmw_fetch_cycles']}"
            )
    return ("PASS" if not failures else "FAIL"), failures


def write_csv_timing_check(
    log_path: Path,
    cfg: dict[str, int | str | float],
) -> tuple[str, list[str]]:
    writes = parse_icl_csv_rows(log_path)
    if not writes:
        return "SKIP", ["no icl_csv_write rows in log"]

    line_bytes = int(cfg["page_size_bytes"])
    capacity_lines = int(cfg["icl_capacity_bytes"]) // line_bytes
    sm = LruStateMachine(capacity_lines)
    pal = PalState(cfg)
    failures: list[str] = []

    for ev in writes:
        t_submit = ev["t_submit"]
        full_page = is_full_page_write(ev, cfg)
        if full_page:
            hit, eviction, victim_lpn = sm.step_write(ev["lpn"])
            rmw = False
        else:
            hit, eviction, rmw, victim_lpn = sm.step_subpage_write(ev["lpn"])

        writeback_done = t_submit
        fetch_done = t_submit
        if eviction and victim_lpn is not None:
            writeback_done = pal.schedule_page_op(
                victim_lpn, True, line_bytes, t_submit
            )
        if rmw:
            fetch_done = pal.schedule_page_op(ev["lpn"], False, line_bytes, t_submit)

        fast_ack = int(cfg["wb_ack_latency_cycles"]) + int(cfg["host_overhead_cycles"])
        if rmw:
            predicted = max(writeback_done, fetch_done, t_submit) - t_submit + fast_ack
            tolerance = 200
        elif eviction:
            predicted = max(t_submit + fast_ack, writeback_done) - t_submit
            tolerance = 200
        else:
            predicted = fast_ack
            tolerance = 60

        delta = ev["latency_cycles"] - predicted
        if abs(delta) > tolerance:
            failures.append(
                f"write seq {ev['seq']} lpn {ev['lpn']} hit={hit} "
                f"evict={eviction} rmw={rmw}: model_lat={ev['latency_cycles']} "
                f"predicted={predicted} delta={delta} tol={tolerance}"
            )
        if ev.get("has_rmw_fields", 0) and rmw:
            predicted_fetch = fetch_done - t_submit
            fetch_delta = ev["icl_rmw_fetch_cycles"] - predicted_fetch
            if abs(fetch_delta) > 200:
                failures.append(
                    f"write seq {ev['seq']} lpn {ev['lpn']}: "
                    f"fetch_cycles={ev['icl_rmw_fetch_cycles']} "
                    f"predicted_fetch={predicted_fetch} "
                    f"delta={fetch_delta} tol=200"
                )

    return ("PASS" if not failures else "FAIL"), failures


def eviction_order_timing_check(
    log_path: Path, cfg: dict[str, int | str | float]
) -> tuple[str, list[str]]:
    fill_pat = re.compile(
        r"^icl_eviction_order_fill\s+page\s+(\d+)\s+cycles\s+(\d+)\s*$"
    )
    evict_pat = re.compile(
        r"^icl_eviction_order_evict\s+expected_victim\s+(\d+)\s+page\s+(\d+)\s+cycles\s+(\d+)\s*$"
    )
    fills: list[tuple[int, int]] = []
    evicts: list[tuple[int, int, int]] = []
    for line in log_path.read_text(errors="replace").splitlines():
        m = fill_pat.match(line)
        if m:
            fills.append((int(m.group(1)), int(m.group(2))))
            continue
        m = evict_pat.match(line)
        if m:
            evicts.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
    if not fills and not evicts:
        return "SKIP", ["no icl_eviction_order_* rows in log"]
    failures: list[str] = []
    for page, measured in fills:
        predicted, tolerance = predict_fill_page(page, cfg)
        delta = measured - predicted
        if abs(delta) > tolerance:
            failures.append(
                f"fill page {page}: measured={measured} predicted={predicted} "
                f"delta={delta} tolerance={tolerance}"
            )
    for expected_victim, page, measured in evicts:
        predicted, tolerance = predict_fill_page(page, cfg)
        delta = measured - predicted
        if abs(delta) > tolerance:
            failures.append(
                f"evict page {page} (expected_victim {expected_victim}): "
                f"measured={measured} predicted={predicted} "
                f"delta={delta} tolerance={tolerance}"
            )
    return ("PASS" if not failures else "FAIL"), failures


def sustained_timing_check(log_path: Path) -> tuple[str, list[str]]:
    header_pat = re.compile(
        r"^icl_sustained\s+qd\s+(\d+)\s+writes\s+(\d+)\s+working_set_pages\s+(\d+)\s+elapsed_cycles\s+(\d+)\s*$"
    )
    p50_pat = re.compile(r"^icl_sustained_p50_cycles\s+(\d+)\s*$")
    p99_pat = re.compile(r"^icl_sustained_p99_cycles\s+(\d+)\s*$")
    elapsed = None
    p50 = None
    p99 = None
    for line in log_path.read_text(errors="replace").splitlines():
        m = header_pat.match(line)
        if m:
            elapsed = int(m.group(4))
            continue
        m = p50_pat.match(line)
        if m:
            p50 = int(m.group(1))
            continue
        m = p99_pat.match(line)
        if m:
            p99 = int(m.group(1))
    if elapsed is None or p50 is None or p99 is None:
        return "SKIP", ["icl_sustained header / p50 / p99 not found"]
    failures: list[str] = []
    if not (p50 <= p99):
        failures.append(f"p50 ({p50}) > p99 ({p99})")
    # Working-set > capacity in the metasim fixture: every write should fall in
    # the eviction-backed cycle range. Allow a wide tolerance to keep this check
    # robust against minor model drift.
    if not (500_000 <= p50 <= 1_000_000):
        failures.append(f"p50 ({p50}) outside sanity band [500k, 1M]")
    if not (500_000 <= p99 <= 1_000_000):
        failures.append(f"p99 ({p99}) outside sanity band [500k, 1M]")
    return ("PASS" if not failures else "FAIL"), failures


def read_latency_check(
    reads: list[dict[str, int]],
    cfg: dict[str, int | str | float],
) -> tuple[str, list[str]]:
    """Check the model-side read latency (t_ack - t_submit) per row.

    Hits short-circuit NAND: model returns t_arrival + host_overhead, so
    model_lat == host_overhead_cycles (tolerance 50). Misses go through PAL
    and the prediction mirrors submit_timing's cold-read math (tolerance 200).
    """
    if not reads:
        return "SKIP", ["no icl_csv_read rows in log"]
    failures: list[str] = []
    host_overhead = int(cfg["host_overhead_cycles"])
    for ev in reads:
        model_lat = ev["latency_cycles"]
        if bool(ev["was_read_hit"]):
            delta = model_lat - host_overhead
            if abs(delta) > 50:
                failures.append(
                    f"read seq {ev['seq']} lpn {ev['lpn']} hit=True: "
                    f"model_lat={model_lat} predicted={host_overhead} "
                    f"delta={delta} tol=50"
                )
        else:
            predicted = predict_read_miss_model_latency(ev["lpn"], cfg)
            delta = model_lat - predicted
            if abs(delta) > 200:
                failures.append(
                    f"read seq {ev['seq']} lpn {ev['lpn']} hit=False: "
                    f"model_lat={model_lat} predicted={predicted} "
                    f"delta={delta} tol=200"
                )
    return ("PASS" if not failures else "FAIL"), failures


def run_benchmark(
    name: str,
    config_path: Path,
    log_path: Path,
    timing_fn,
) -> tuple[str, str, Optional[str], Optional[str], list[str]]:
    cfg = load_cfg(parse_ini(config_path))
    writes = parse_icl_csv_rows(log_path)
    reads = parse_icl_csv_read_rows(log_path)
    events = merge_csv_events(writes, reads)
    line_bytes = int(cfg["page_size_bytes"])
    capacity_lines = int(cfg["icl_capacity_bytes"]) // line_bytes

    timing_status, timing_msgs = timing_fn(log_path, cfg)
    sm_status, sm_msgs = state_machine_check(events, capacity_lines, cfg)
    rmw_status: Optional[str] = None
    rmw_msgs: list[str] = []
    if writes:
        rmw_status, rmw_msgs = rmw_flag_check(events, capacity_lines, cfg)
    messages = [f"timing: {m}" for m in timing_msgs] + [
        f"state-machine: {m}" for m in sm_msgs
    ] + [
        f"rmw-flag: {m}" for m in rmw_msgs
    ]
    read_status: Optional[str] = None
    if reads:
        read_status, lat_msgs = read_latency_check(reads, cfg)
        messages.extend(f"read-latency: {m}" for m in lat_msgs)
    return timing_status, sm_status, read_status, rmw_status, messages


def no_timing_check(log_path: Path, cfg) -> tuple[str, list[str]]:
    """Stub for benchmarks where state-machine + read-latency cover correctness
    and there is no analytical timing model worth wiring up separately."""
    del log_path
    del cfg
    return "PASS", []


def discover_results_dir(results_dir: Path) -> list[tuple[str, Path, Path, object]]:
    """Returns (name, config, log, timing_fn) tuples for each subdir present."""
    subdirs = {
        "icl-fill": ("fill", fill_timing_check),
        "icl-eviction-order": (
            "eviction_order",
            lambda log, cfg: eviction_order_timing_check(log, cfg),
        ),
        "icl-sustained": (
            "sustained",
            lambda log, cfg: sustained_timing_check(log),
        ),
        "icl-ryow": ("ryow", no_timing_check),
        "icl-read-hit-latency": ("read_hit_latency", no_timing_check),
        "icl-eviction-then-read": ("eviction_then_read", no_timing_check),
        "icl-rmw-cold": ("rmw_cold", write_csv_timing_check),
        "icl-rmw-warm": ("rmw_warm", write_csv_timing_check),
        "icl-rmw-eviction": ("rmw_eviction", write_csv_timing_check),
    }
    out: list[tuple[str, Path, Path, object]] = []
    for name, (dirname, timing_fn) in subdirs.items():
        d = results_dir / dirname
        if not d.is_dir():
            continue
        config_path = d / "config.ini"
        log_path = d / "run.log"
        if not config_path.is_file() or not log_path.is_file():
            continue
        out.append((name, config_path, log_path, timing_fn))
    return out


def self_test_rmw_flag_failure() -> int:
    config_text = """\
target_clock_hz = 1000000000
cell_type = SLC
bits_per_cell = 1
n_channels = 1
n_ways = 1
n_dies_per_channel = 1
n_planes = 1
pages_per_block = 1024
page_size_bytes = 16384
bus_speed_mtps = 533
tR_LSB_ns = 77000
tPROG_LSB_ns = 700000
wb_enabled = true
wb_ack_latency_cycles = 20000
host_overhead_cycles = 4072
host_bandwidth_bps = 3300000000
icl_capacity_bytes = 32768
icl_line_size_bytes = 16384
icl_eviction_policy = lru
icl_associativity = full
"""
    bad_log = (
        "icl_csv_write seq=0 tag=0 offset=0 len=8 lpn=0 "
        "t_submit=0 t_ack=132000 latency_cycles=132000 "
        "was_hit=0 was_eviction_triggered=0 victim_lpn=-1 "
        "icl_rmw_triggered=0 icl_rmw_fetch_cycles=0\n"
    )
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        config_path = tmpdir / "config.ini"
        log_path = tmpdir / "run.log"
        config_path.write_text(config_text)
        log_path.write_text(bad_log)
        cfg = load_cfg(parse_ini(config_path))
        writes = parse_icl_csv_rows(log_path)
        events = merge_csv_events(writes, [])
        status, messages = rmw_flag_check(events, 2, cfg)
        if status != "FAIL":
            print("self-test-rmw: expected FAIL after flipped rmw flag")
            for msg in messages:
                print(f"  {msg}")
            return 1
        print("self-test-rmw: PASS (flipped rmw flag is detected)")
        for msg in messages:
            print(f"  {msg}")
        return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path,
                        help="INI config for single-benchmark mode (legacy --log path)")
    parser.add_argument("--log", type=Path,
                        help="run.log for single-benchmark mode (defaults to fill timing)")
    parser.add_argument("--results-dir", type=Path,
                        help="icl_3a_metasim baseline directory; runs fill/eviction_order/sustained")
    parser.add_argument("--self-test-rmw", action="store_true",
                        help="run synthetic CSV smoke test that flips one RMW flag")
    args = parser.parse_args()

    if args.self_test_rmw:
        return self_test_rmw_flag_failure()

    runs: list[tuple[str, Path, Path, object]] = []
    if args.results_dir is not None:
        runs = discover_results_dir(args.results_dir)
        if not runs:
            print(f"verify_icl_3a: no fill/eviction_order/sustained subdirs under {args.results_dir}",
                  file=sys.stderr)
            return 2
    elif args.config is not None and args.log is not None:
        runs = [("icl-fill", args.config, args.log, fill_timing_check)]
    else:
        parser.error("provide either --results-dir or both --config and --log")

    overall_failures = 0
    for name, config_path, log_path, timing_fn in runs:
        timing_status, sm_status, read_status, rmw_status, messages = run_benchmark(
            name, config_path, log_path, timing_fn
        )
        summary = f"[{name}] timing: {timing_status}, state-machine: {sm_status}"
        if rmw_status is not None:
            summary += f", rmw-flag: {rmw_status}"
        if read_status is not None:
            summary += f", read-latency: {read_status}"
        print(summary)
        for msg in messages:
            print(f"  {msg}")
        if (
            timing_status == "FAIL"
            or sm_status == "FAIL"
            or rmw_status == "FAIL"
            or read_status == "FAIL"
        ):
            overall_failures += 1

    print("PASS" if overall_failures == 0 else "FAIL")
    return 0 if overall_failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
