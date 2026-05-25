# ICL 3a/3b/3d metasim baselines

Regenerated on 2026-05-14 on Crackle07 with
`FireSimRocketSSDLatencyConfig` under FireSim Verilator metasim.

These are bounded smoke baselines for the Phase 3 ICL paths, not full-capacity
PM1725a performance sweeps. The fixture uses PM1725a-class TLC timing with a
tiny 4-line ICL cache so writeback, read-hit, and sub-page RMW behavior finish
in metasim:

- Config: `tests/ssd_configs/icl_3a_metasim.ini`
- Cache: 4 lines, 16 KiB per line, fully associative LRU
- Geometry: 16 channels, 4 dies/channel, 2 planes, 16 KiB pages
- Timings: TLC `tPROG_LSB=700000` cycles, `wb_ack=20000` cycles,
  `host_overhead=4072` cycles, host bandwidth cap `3.3 GB/s`
- Binaries:
  - `blkdev-icl-fill-metasim.riscv`: `ICL_CAPACITY_LINES=4`
  - `blkdev-icl-eviction-order-metasim.riscv`: `ICL_CAPACITY_LINES=4`
  - `blkdev-icl-sustained-metasim.riscv`: `ICL_SUSTAINED_WRITES=8`,
    `ICL_WORKING_SET_PAGES=8`, `ICL_QD=8`
  - `blkdev-icl-ryow-metasim.riscv`
  - `blkdev-icl-read-hit-latency-metasim.riscv`: `ICL_CAPACITY_LINES=4`
  - `blkdev-icl-eviction-then-read-metasim.riscv`: `ICL_CAPACITY_LINES=4`
  - `blkdev-icl-rmw-cold-metasim.riscv`: `ICL_RMW_WRITES=4`
  - `blkdev-icl-rmw-warm-metasim.riscv`: `ICL_RMW_PAGES=4`
  - `blkdev-icl-rmw-eviction-metasim.riscv`: `ICL_CAPACITY_LINES=4`,
    `ICL_RMW_EVICT_WRITES=4`

Baseline results:

| Benchmark | Key result |
| --- | --- |
| `fill` | First four writes: `30282..30384` cycles; eviction-backed writes: `737038..737048` cycles |
| `eviction_order` | Victims observed in deterministic LRU order: `0, 1, 2, 3` |
| `sustained` | `qd=8`, `writes=8`, `elapsed=893955`, `p50=736635`, `p99=737048` cycles |
| `ryow` | Write LPN 0, read LPN 0; read hit model latency `4072` cycles |
| `read_hit_latency` | Four read hits after fill, each `was_read_hit=1`, model latency `4072` cycles |
| `eviction_then_read` | Target LPN 0 evicted, later read is `was_read_hit=0`, model latency `116778` cycles |
| `rmw_cold` | Four sub-page cold misses, each `icl_rmw_triggered=1`, fetch `107741` cycles |
| `rmw_warm` | Full-page fills followed by sub-page hits, each hit `icl_rmw_triggered=0` |
| `rmw_eviction` | Sub-page misses with eviction, victims `0, 1, 2, 3`, each total model latency `754813` cycles |

Verification:

```text
[icl-fill] timing: PASS, state-machine: PASS, rmw-flag: PASS
[icl-eviction-order] timing: PASS, state-machine: PASS, rmw-flag: PASS
[icl-sustained] timing: PASS, state-machine: PASS, rmw-flag: PASS
[icl-ryow] timing: PASS, state-machine: PASS, rmw-flag: PASS, read-latency: PASS
[icl-read-hit-latency] timing: PASS, state-machine: PASS, rmw-flag: PASS, read-latency: PASS
[icl-eviction-then-read] timing: PASS, state-machine: PASS, rmw-flag: PASS, read-latency: PASS
[icl-rmw-cold] timing: PASS, state-machine: PASS, rmw-flag: PASS
[icl-rmw-warm] timing: PASS, state-machine: PASS, rmw-flag: PASS
[icl-rmw-eviction] timing: PASS, state-machine: PASS, rmw-flag: PASS
PASS
```

`run.log` is the primary reference log for each benchmark. `uart_tail.txt`
keeps the metasim pass line without storing the full decoded `spike-dasm`
stream. `icl_csv_write.txt` and `icl_csv_read.txt` are extracted from
`run.log` and are what `tests/verify_icl_3a.py` uses for active state-machine,
read-latency, and RMW-flag checks.
