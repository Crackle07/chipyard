# SSD Storage Model Port Plan to `firesim-f2-bump`

This document is the working port plan for moving the SSD timing/cache model from the local `SSD` branch onto upstream Chipyard `firesim-f2-bump`.

The local workspace has one Chipyard checkout at `/scratch/anishs/chipyard`. The source branch is `SSD`; the target branch is remote ref `upstream/firesim-f2-bump`.

## Execution Status

Last updated from the live workspace after running Phases 0, 1, and 2.

Phase 0 is complete. The dirty PAL/ICL work is now committed on `SSD` as replayable commits:

| Commit | Purpose |
|---|---|
| `85c3076e` | Records the new `generators/testchipip` submodule pointer |
| `534092d3` | Adds `ssd_latency_model.{cc,h}` |
| `962bb8cc` | Adds SSD host-timing mode to the block-device bridge |
| `6e315a59` | Adds C++ and Scala bridge tests |
| `dba5b29e` | Adds baremetal validation workloads/configs/scripts |
| `dcf0bf11` | Adds validation baselines and this port plan |

The `generators/testchipip` submodule also has one new local commit:

| Commit | Purpose |
|---|---|
| `450a527` | Allows 256 block-device trackers and clamps queue-count CSR widths |

Backups created before committing:

```bash
/scratch/anishs/ssd-superproject-dirty.patch
/scratch/anishs/ssd-testchipip-dirty.patch
/scratch/anishs/ssd-superproject-untracked.tar.gz
```

Generated FireSim run artifacts were stashed inside `sims/firesim`:

```bash
cd /scratch/anishs/chipyard/sims/firesim
git stash list
# stash@{0}: On (no branch): generated metasim outputs before SSD port
```

Phase 1 was rerun at `dcf0bf11`. The source branch had 9 unique commits relative to `upstream/main` at that point:

```bash
dcf0bf11 Document SSD latency validation plan
dba5b29e Add SSD latency validation workloads
6e315a59 Add SSD block-device bridge tests
962bb8cc Add SSD host-timing mode to block device bridge
534092d3 Add SSD PAL and ICL latency model
85c3076e Bump testchipip for SSD block-device tracker depth
18a644bd Document SSD block bridge architecture
c00e6edd Add SSD FireSim sample configs
c0699789 Update SSD submodule revisions
```

Phase 2 was also rerun after Phase 0. `git merge-tree` predicts one textual conflict in `generators/firechip/chip/src/main/scala/TargetConfigs.scala`, caused by the old CTC hunk from `c0699789` conflicting with the newer upstream CTC fix already present in `firesim-f2-bump`. Do not replay that old CTC hunk. The block-device bridge files merge textually, but still need semantic review because they change the bridge ABI and timing ownership.

## Phase 0: Make the Source Branch Replayable

Goal: turn the dirty PAL/ICL research state into clean commits before creating the F2 port branch. This prevents later cherry-picks from carrying only the already committed documentation/submodule updates while dropping the live model files.

### 0.1 Snapshot and Back Up

```bash
cd /scratch/anishs/chipyard
git switch SSD
git status --short --untracked-files=all

git branch ssd-pal-icl-source-backup || true
git diff > ../ssd-superproject-dirty.patch
git -C generators/testchipip diff > ../ssd-testchipip-dirty.patch
```

Do not commit generated FireSim run outputs:

```bash
git -C sims/firesim status --short --untracked-files=all
git -C sims/firesim stash push -u -m "generated metasim outputs before SSD port"
```

### 0.2 Commit Dirty `testchipip` Submodule Work

Submodule commits must happen inside the submodule first.

```bash
cd /scratch/anishs/chipyard/generators/testchipip
git status --short
git add src/main/scala/iceblk/BlockDevice.scala
git diff --cached
git commit -m "Allow 256 block-device trackers"
git log --oneline -2
```

Then record the new submodule pointer in Chipyard:

```bash
cd /scratch/anishs/chipyard
git add generators/testchipip
git commit -m "Bump testchipip for SSD block-device tracker depth"
```

### 0.3 Commit PAL/ICL Model and Integration in Pieces

```bash
git add \
  generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.h
git commit -m "Add SSD PAL and ICL latency model"
```

```bash
git add \
  generators/firechip/bridgeinterfaces/src/main/scala/BlockDevice.scala \
  generators/firechip/goldengateimplementations/src/main/scala/BlockDevBridgeModule.scala \
  generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.h \
  generators/firechip/chip/src/main/scala/TargetConfigs.scala
git commit -m "Add SSD host-timing mode to block device bridge"
```

```bash
git add \
  generators/firechip/bridgestubs/src/main/scala/iceblk/BlockDevModule.scala \
  generators/firechip/bridgestubs/src/test/scala/BridgeSuite.scala \
  generators/firechip/bridgestubs/src/main/cc/bridges/test/blockdev_host_timing_unit.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/test/ssd_latency_model_unit.cc
git commit -m "Add SSD block-device bridge tests"
```

```bash
git add \
  tests/CMakeLists.txt \
  tests/blkdev-*.c \
  tests/ssd_configs \
  tests/compare_csvs.py \
  tests/verify_*.py
git commit -m "Add SSD latency validation workloads"
```

Optional baseline/docs commit:

```bash
git add docs/ssd-bridge-architecture.md tests/ssd_latency_results port-plan.md
git commit -m "Document SSD latency validation plan"
```

Finish Phase 0 with:

```bash
git status --short --untracked-files=all
git log --oneline --decorate -12
```

## Phase 1: Recon Findings

Commands:

```bash
cd /scratch/anishs/chipyard
git remote get-url upstream || git remote add upstream https://github.com/ucb-bar/chipyard.git
git fetch --no-recurse-submodules upstream main firesim-f2-bump
git rev-parse --abbrev-ref HEAD
git merge-base HEAD upstream/main
git log --oneline upstream/main..HEAD
git diff --name-only $(git merge-base HEAD upstream/main)...HEAD
git submodule status sims/firesim
(cd sims/firesim && git log --oneline -5)
```

Current findings:

- Current branch: `SSD`.
- HEAD: `18a644bdca988a20058382b4f5aef7d5a87c8b5e`, dated `2026-04-29`, `Document SSD block bridge architecture`.
- Merge-base with `upstream/main`: `756ffa75d5c6d92fccc3f0f5260351e257346b76`, dated `2026-04-20`.
- Unique committed fork commits before Phase 0: 3.
- MY_FORK FireSim pin: `b084672c2f8cf32e55d78f73a001074c23f8a2b8`, dated `2024-10-20`.
- F2_BUMP FireSim pin: `4a04e7a5f4094723bf392664b5ff9bbd8c088137`, dated `2026-05-19`.
- F2 FireSim is 85 commits ahead of MY_FORK FireSim.
- No SSD source changes were found inside `sims/firesim`; only generated untracked outputs.

Committed fork commits by intent:

| Commit | Intent |
|---|---|
| `18a644bd` | SSD bridge architecture docs plus 256-bit block-device bridge ABI changes |
| `c00e6edd` | FireSim SSD benchmark sample configs, mostly F1-oriented docs/config samples |
| `c0699789` | Testchipip/FireMarshal submodule bumps plus an unrelated CTC config fix |

## Phase 2: Compare and Classification

| Path | F2 status | Classification | Load-bearing | Notes |
|---|---|---:|---:|---|
| `docs/Simulation/firesim-ssd-bench-sample-configs/*` | New dir absent | TRIVIAL | F1-SPECIFIC / NICE | Port only after updating to F2 sample config values. |
| `docs/ssd-bridge-architecture.md` | Absent | TRIVIAL | NICE | Refresh validation commands for F2. |
| `generators/firechip/bridgeinterfaces/src/main/scala/BlockDevice.scala` | Exists | LIGHT ADJUST | ESSENTIAL | Adds 256-bit beat width and queue-depth params. |
| `generators/firechip/goldengateimplementations/src/main/scala/BlockDevBridgeModule.scala` | Exists | MEDIUM REWRITE | ESSENTIAL | Touches 256-bit MMIO ABI, target-cycle exposure, and host-timing bypass. |
| `generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.{cc,h}` | Exists | MEDIUM REWRITE | ESSENTIAL | Adds 256-bit MMIO, `+blkdev-ssd-configN=`, completion heap, and SSD host timing. |
| `generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.{cc,h}` | New files | LIGHT ADJUST | ESSENTIAL | PAL + ICL timing/cache model. |
| `generators/firechip/chip/src/main/scala/TargetConfigs.scala` | Exists | MEDIUM REWRITE | ESSENTIAL for SSD config | Drop the old CTC hunk from `c0699789`; f2-bump already has the newer upstream CTC fix. |
| `generators/firechip/bridgestubs/src/main/scala/iceblk/BlockDevModule.scala` | Exists | LIGHT ADJUST | NICE | Test harness fix for 256-bit beats. |
| `generators/firechip/bridgestubs/src/test/scala/BridgeSuite.scala` | Exists | LIGHT ADJUST | NICE | Adds SSD host-timing bridge test. |
| `tests/CMakeLists.txt`, `tests/blkdev-*.c` | Parent exists, files new | LIGHT ADJUST | NICE / VALIDATION | Baremetal PAL/ICL validation programs. |
| `tests/ssd_configs/*`, `tests/verify_*.py`, `tests/compare_csvs.py`, `tests/ssd_latency_results/**` | New | TRIVIAL | NICE / EXPERIMENTAL | Useful regression baselines, not required to boot SSD model. |
| `generators/testchipip` submodule | F2 pin is `5dca05b`; fork pin is `7287882` plus dirty changes | SEPARATE | ESSENTIAL | Replay `7287882` and the dirty tracker-depth edits. |
| `software/firemarshal` submodule | F2 pin is `21119e5`; fork pin is `e0d4bb0` | SEPARATE | NICE / Linux validation | Needed for `br-ssd-bench`, not for the bridge model itself. |
| `sims/firesim` submodule | F2 pin is newer | SEPARATE / DROP OLD PIN | ESSENTIAL to keep F2 | Do not rewind to `b084672c2`. |

SSD-adjacent f2-bump search:

```bash
git diff --name-only --ignore-submodules=all -G'blockdev|SimBlock|storage|ssd|nvme|ftl|nand' upstream/main..upstream/firesim-f2-bump
```

No direct blockdev/SSD/NVMe/FTL/NAND changes were found in the Chipyard superproject. The only false-positive keyword hit was `secretstorage` in conda lockfiles.

## Phase 3: Load-Bearing Pieces

ESSENTIAL:

- `generators/testchipip` block-device widening to 256-bit beats.
- Firechip bridge ABI changes in `BlockDevice.scala`, `BlockDevBridgeModule.scala`, and `blockdev.{cc,h}`.
- New `ssd_latency_model.{cc,h}`.
- Host-timing path: `bdev_target_cycle`, `bdev_host_timing`, completion heap, `+blkdev-ssd-configN=`.
- Chipyard config fragment adding `FireSimRocketSSDLatencyConfig`.
- Backing-file plusarg path via `+blkdev0=<disk>`.

NICE-TO-HAVE:

- BridgeSuite SSD host-timing test.
- Baremetal PAL/ICL tests and CSV regression tooling.
- FireMarshal `br-ssd-bench` workload.
- Architecture docs.

DROP / DEFER:

- Generated `sims/firesim/*.csv` and `sims/firesim/sim/*.out`.
- `tests/ssd_latency_results/**` if doing a minimal functionality-first port.

F1-SPECIFIC:

- Existing sample FireSim docs/configs under `docs/Simulation/firesim-ssd-bench-sample-configs/`; update them before using on F2.

## Phase 4: Port Plan

### 4.1 Create the F2 Port Branch

Run only after Phase 0 is clean.

```bash
cd /scratch/anishs/chipyard
git fetch --no-recurse-submodules upstream main firesim-f2-bump
git switch -c ssd-port-f2 upstream/firesim-f2-bump
git submodule update --init --recursive generators/testchipip software/firemarshal sims/firesim
```

### 4.2 Port `testchipip` First

```bash
cd /scratch/anishs/chipyard/generators/testchipip
git cherry-pick 7287882d6ab04ae7d1ecf87ad15e6f0b953e6ee3
# Then cherry-pick or manually replay the Phase 0 tracker-depth commit.
```

### 4.3 Port the 256-bit Firechip Bridge ABI

Files:

- `generators/firechip/bridgeinterfaces/src/main/scala/BlockDevice.scala`
- `generators/firechip/goldengateimplementations/src/main/scala/BlockDevBridgeModule.scala`
- `generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.cc`
- `generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.h`
- `generators/firechip/bridgestubs/src/main/scala/iceblk/BlockDevModule.scala`

Minimum platform-agnostic target elaboration:

```bash
cd /scratch/anishs/chipyard
source env.sh

make -C sims/firesim-staging firrtl \
  SBT_PROJECT=firechip \
  MODEL=FireSim \
  MODEL_PACKAGE=firechip.chip \
  VLOG_MODEL=FireSim \
  CONFIG=FireSimRocketSSDLatencyConfig \
  CONFIG_PACKAGE=firechip.chip \
  GENERATOR_PACKAGE=chipyard \
  EXTRA_CHISEL_OPTIONS=--emit-legacy-sfc \
  TB=unused \
  TOP=unused
```

F1-isolated Golden Gate check:

```bash
cd /scratch/anishs/chipyard/sims/firesim
source sourceme-manager.sh

make -C sim compile \
  TARGET_PROJECT=firesim \
  TARGET_PROJECT_MAKEFRAG=../../generators/firechip/chip/src/main/makefrag/firesim \
  TARGET_CONFIG=FireSimRocketSSDLatencyConfig \
  PLATFORM=f1 \
  PLATFORM_CONFIG=BaseF1Config
```

### 4.4 Port Chipyard SSD Config Wiring

Add only the SSD config fragments. Do not carry the old CTC hunk from `c0699789`.

Expected additions:

- `WithSSDLatencyBlockDeviceDepth`
- `FireSimRocketSSDLatencyConfig`

### 4.5 Port SSD C++ Model and Host Timing

Files:

- `generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc`
- `generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.h`
- Host-timing edits in `blockdev.{cc,h}`
- Host-timing mode in `BlockDevBridgeModule.scala`

### 4.6 Port FireMarshal Workload and F2 Sample Configs Last

```bash
cd /scratch/anishs/chipyard/software/firemarshal
git cherry-pick e0d4bb0bca592134713e120735b1ec50a1555acd
./marshal -v build boards/firechip/base-workloads/br-ssd-bench.json
./marshal install boards/firechip/base-workloads/br-ssd-bench.json
```

F2 sample configs must use `PLATFORM: f2`, `BaseF2Config`, and `bit-builder-recipes/f2.yaml`.

## FireSim Submodule Strategy

Keep f2-bump's FireSim submodule pin at `4a04e7a5f` and do not maintain an SSD FireSim fork yet. Recon found no SSD source changes inside `sims/firesim`; the host bridge driver currently lives in Chipyard's `generators/firechip`, not the FireSim submodule.

If later FireSim internals must change, create a small FireSim fork branch based exactly on `4a04e7a5f`, replay only those commits there, and pin Chipyard to that fork commit.

## F2 Branch Stability

Commands:

```bash
git log --oneline upstream/main..upstream/firesim-f2-bump | wc -l
git log --oneline upstream/firesim-f2-bump..upstream/main | wc -l
git log -1 --format='%H %cI %s' upstream/firesim-f2-bump
git -C sims/firesim show -s --format='%H %cI %s' 4a04e7a5f
```

Findings:

- f2-bump is 3 commits ahead of `upstream/main`.
- f2-bump is 0 commits behind `upstream/main`.
- Chipyard f2-bump HEAD: `5845cf17`, dated `2026-05-19`.
- FireSim f2 pin: `4a04e7a5f`, dated `2026-05-19`.

Assessment: usable as a downstream port base if pinned by SHA, but it is a fresh F2 development branch, not a stable release branch.

## Effort Estimate

| Phase | Estimate |
|---|---:|
| Phase 0 clean commits | 1-2 hours |
| Baseline branch and submodule setup | 1-2 hours |
| Testchipip + 256-bit bridge ABI | half-day |
| SSD C++ host-timing model port | full-day |
| Baremetal PAL/ICL validation | half-day |
| FireMarshal + F2 manager configs | half-day |
| Full F2 FPGA build and Linux workload validation | multi-day |

## Risk Callouts

- `TargetConfigs.scala`: old CTC hunk conflicts with upstream `48f904ae`; drop it.
- `generators/testchipip/src/main/scala/iceblk/BlockDevice.scala`: `nTrackers=256` requires the dirty `nTrackers <= 256` and queue-count-width edits.
- 256-bit ABI must land atomically across testchipip, Firechip Scala, and C++ driver.
- `BlockDevBridgeModule.scala`: host-timing mode changes stall behavior and bypasses `DynamicLatencyPipe`; this is the highest semantic risk.
- `blockdev.h`: fork dirty tree raises `MAX_REQ_LEN` to 32 while testchipip C simulator commit shows 16; confirm intended hardware/software contract.
- F2 FireSim adds `simif_f2.cc`, F2 manager/build recipes, and conda changes; bridge driver API appears stable, but hardware deployment path is young.

## Phase 5: Validation

Run these gates in order. Do not move to the next gate unless the current one passes, except Gate 5, which is deferred F2-platform coverage.

### GATE 1: C++ Unit Tests

Run after the SSD C++ model and `blockdev` host-timing edits are ported.

```bash
cd /scratch/anishs/chipyard
mkdir -p /tmp/ssd-unit

${CXX:-g++} -std=c++17 -O2 -Wall -Wextra \
  -I generators/firechip/bridgestubs/src/main/cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/test/ssd_latency_model_unit.cc \
  -o /tmp/ssd-unit/ssd_latency_model_unit

/tmp/ssd-unit/ssd_latency_model_unit

${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -DBLOCKDEV_UNIT_TEST \
  -I generators/firechip/bridgestubs/src/main/cc \
  -I sims/firesim/sim/midas/src/main/cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/test/blockdev_host_timing_unit.cc \
  -o /tmp/ssd-unit/blockdev_host_timing_unit

/tmp/ssd-unit/blockdev_host_timing_unit
```

Pass criteria: both binaries exit `0`.

Failure means: PAL/ICL model logic, INI parsing, completion heap behavior, or `blockdev_t` host-timing integration is broken.

Halt on failure: yes.

### GATE 2: Scala Bridge Tests

Run after the Scala bridge ABI and host-timing control path are ported.

```bash
cd /scratch/anishs/chipyard
source env.sh

java -jar scripts/sbt-launch.jar \
  ';project firechip_bridgestubs; testOnly firechip.bridgestubs.BlockDevF1Test -- -z "copy from one device to another with SSD host timing"'
```

Pass criteria: sbt exits `0`; ScalaTest reports the SSD host-timing copy test passed.

Failure means: bridge generator wiring, `BlockDevBridgeModule`, MMIO register names/order, or Scala/C++ bridge test integration is broken.

Halt on failure: yes.

### GATE 3: Baseline Metasim Without SSD Model

This confirms the 256-bit ABI did not break the stock block device.

```bash
cd /scratch/anishs/chipyard
source env.sh
cmake -S tests -B tests/build -D CMAKE_BUILD_TYPE=Debug
cmake --build tests/build --target blkdev

cd sims/firesim
source sourceme-manager.sh
set -o pipefail

make -C sim run-verilator \
  TARGET_PROJECT=firesim \
  TARGET_PROJECT_MAKEFRAG=../../generators/firechip/chip/src/main/makefrag/firesim \
  TARGET_CONFIG=FireSimRocketConfig \
  PLATFORM=f1 \
  PLATFORM_CONFIG=BaseF1Config \
  SIM_BINARY=/scratch/anishs/chipyard/tests/build/blkdev.riscv \
  MIDAS_LEVEL_SIM_ARGS=+max-cycles=5000000 \
  2>&1 | tee /tmp/ssd-gate3-blkdev.log

grep -q "All correct" /tmp/ssd-gate3-blkdev.log
```

Pass criteria: `make` exits `0` and the log contains `All correct`.

Failure means: the 256-bit block-device ABI port broke baseline block-device function, independent of SSD timing.

Halt on failure: yes.

### GATE 4: SSD Metasim With Correctness Check

Use known-good PAL read/write baseline `tests/ssd_latency_results/pal_read_write_compare.csv`. Tolerance: 5% relative movement.

```bash
cd /scratch/anishs/chipyard
source env.sh
cmake --build tests/build --target blkdev-pal-parallelism

cat >/tmp/ssd-pal-read-write.ini <<'EOF'
[sim]
FixedPolicy=false
TargetClockHz=1000000000

[pal]
Channel=8
Way=1
Die=1
Plane=1
PageSize=512
DMASpeed=512000000000
NANDType=SLC
Read.LSB=5000
Program.LSB=25000

[hil]
CmdOverhead=2
CompletionOverhead=3
HostBandwidth=512000000000
EOF

truncate -s 16M /tmp/ssd-pal-read-write.disk

cd sims/firesim
source sourceme-manager.sh
set -o pipefail

make -C sim run-verilator \
  TARGET_PROJECT=firesim \
  TARGET_PROJECT_MAKEFRAG=../../generators/firechip/chip/src/main/makefrag/firesim \
  TARGET_CONFIG=FireSimRocketSSDLatencyConfig \
  PLATFORM=f1 \
  PLATFORM_CONFIG=BaseF1Config \
  SIM_BINARY=/scratch/anishs/chipyard/tests/build/blkdev-pal-parallelism.riscv \
  MIDAS_LEVEL_SIM_ARGS=+max-cycles=5000000 \
  EXTRA_SIM_ARGS='+blkdev0=/tmp/ssd-pal-read-write.disk +blkdev-ssd-config0=/tmp/ssd-pal-read-write.ini' \
  2>&1 | tee /tmp/ssd-gate4-pal.log

grep -q "Done" /tmp/ssd-gate4-pal.log
```

Create and compare the CSV:

```bash
cd /scratch/anishs/chipyard
mkdir -p /tmp/ssd-gate4

awk '
/^read_stride8_cycles / {rs=$2}
/^read_contiguous_cycles / {rc=$2}
/^read_stride8_over_contiguous_ratio / {rr=$2}
/^write_stride8_cycles / {ws=$2}
/^write_contiguous_cycles / {wc=$2}
/^write_stride8_over_contiguous_ratio / {wr=$2}
END {
  if (!rs || !rc || !rr || !ws || !wc || !wr) { exit 2 }
  print "date,source,benchmark,target_clock_hz,channels,ways,dies,planes,page_size_bytes,operation,read_lsb_ns,program_lsb_ns,cmd_overhead_cycles,completion_overhead_cycles,dma_speed_Bps,host_bandwidth_Bps,stride8_cycles,contiguous_cycles,stride8_over_contiguous_ratio,notes"
  print "port,port_metasim,blkdev-pal-parallelism,1000000000,8,1,1,1,512,read,5000,25000,2,3,512000000000,512000000000," rs "," rc "," rr ",port validation"
  print "port,port_metasim,blkdev-pal-parallelism,1000000000,8,1,1,1,512,write,5000,25000,2,3,512000000000,512000000000," ws "," wc "," wr ",port validation"
}' /tmp/ssd-gate4-pal.log > /tmp/ssd-gate4/pal_read_write_compare.csv

python3 tests/compare_csvs.py \
  tests/ssd_latency_results/pal_read_write_compare.csv \
  /tmp/ssd-gate4/pal_read_write_compare.csv \
  --threshold 0.05
```

Pass criteria: metasim exits `0`, log contains `Done`, and `compare_csvs.py` exits `0` with `status ok`.

Failure means: SSD host-timing mode, PAL scheduling, target-cycle alignment, or response/write-ack release timing regressed.

Halt on failure: yes.

### GATE 5: F2 Platform Compile Only

Deferred platform check. No F2 run, no AGFI, no instance required.

```bash
cd /scratch/anishs/chipyard/sims/firesim
source sourceme-manager.sh

make -C sim compile \
  TARGET_PROJECT=firesim \
  TARGET_PROJECT_MAKEFRAG=../../generators/firechip/chip/src/main/makefrag/firesim \
  TARGET_CONFIG=FireSimRocketSSDLatencyConfig \
  PLATFORM=f2 \
  PLATFORM_CONFIG=BaseF2Config
```

Pass criteria: `make` exits `0`.

Failure means: the SSD target/bridge compiles under F1-style metasim but conflicts with F2 platform config, Golden Gate/F2 collateral, or f2-bump platform assumptions.

Halt on failure: no for local SSD correctness; yes before any F2 deployment milestone.
