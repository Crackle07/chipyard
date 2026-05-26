# Phase 4 Step 1-2 Status

- Timestamp: 2026-05-25T19:12:33-07:00
- Summary: Step 1-2 completed; `ssd-port-f2` was rooted at the pinned f2-bump SHA, `testchipip` cherry-picked cleanly, and the superproject now points at the ported `testchipip` commit.
- Port branch root SHA: `5845cf1700a38569bbe47d8cb11c24fa772fa84a`
- Submodule SHAs after init:
  - `generators/testchipip`: `5dca05bef9a9d7b135e18543379bc782df80ce40`
  - `sims/firesim`: `4a04e7a5f4094723bf392664b5ff9bbd8c088137`
  - `software/firemarshal`: `21119e5ce922ff9302f0bc9d2d7349c77f7ac064`
- `testchipip` cherry-pick outcome: clean
- Source `testchipip` commit: `450a527`
- Resulting `testchipip` commit on f2 base: `f86a977`
- Superproject bump commit: `ce3bab5a`
- Note: recursive submodule init left `sims/firesim/utils/fireperf/FlameGraph/` as untracked content; it was not staged or committed.

# Phase 4 Step 3 Status

- Timestamp: 2026-05-25T20:00:37-07:00
- Summary: Step 3 completed; Chipyard-side SSD config wiring and the Scala 256-bit block-device ABI declaration were ported onto `ssd-port-f2`, and Gate 0 elaboration passed.
- Step 3 commit SHA: `0bcb34bf`
- Gate 0 elaboration: passed, exit code 0
- Gate 0 command used:

```bash
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

- Gate 0 output notes:
  - The `sims/firesim-staging firrtl` target existed, so the `sims/firesim/sim verilog PLATFORM=f1` fallback was not used.
  - The elaborated design showed 256 block-device trackers in the L2 client map.
  - The run emitted many FIRRTL2 unsupported-annotation warnings, but the make target exited successfully.

## SSD Commits Touching Step 3 Files

Unique-to-SSD commits touching the in-scope files:

- `6e315a59 Add SSD block-device bridge tests`
  - `generators/firechip/bridgestubs/src/main/scala/iceblk/BlockDevModule.scala`
- `962bb8cc Add SSD host-timing mode to block device bridge`
  - `generators/firechip/bridgeinterfaces/src/main/scala/BlockDevice.scala`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `18a644bd Document SSD block bridge architecture`
  - `generators/firechip/bridgeinterfaces/src/main/scala/BlockDevice.scala`
- `c0699789 Update SSD submodule revisions`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`

The literal `git log --oneline SSD -- <three Step 3 files>` command also reported inherited/non-unique history:

- `67183a0e Clean up commented code`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `94ecc0a7 Clean up CTC configs`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `7b2a3dc0 Support multiple CTC ports per chip`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `d88c1d5c CTC working in metasims`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `e1ce4954 Update to newer rocket-chip`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `4f7ce37b fix firesim config`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `b40330a4 more cleanup: remove cease io, clean up makefiles, update submodules & configs`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `322e6462 Merge branch 'main' of https://github.com/ucb-bar/chipyard into graphics`
- `4c06e55d Improve naming of Serial-TL PhyParams`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`
- `73dbdf17 Rework package paths`
  - `generators/firechip/bridgeinterfaces/src/main/scala/BlockDevice.scala`
  - `generators/firechip/bridgestubs/src/main/scala/iceblk/BlockDevModule.scala`
  - `generators/firechip/chip/src/main/scala/TargetConfigs.scala`

## Step 3 Deviations

- Did not cherry-pick whole commits; manually applied only the SSD-related hunks to the three in-scope Scala files.
- Dropped the old CTC hunk from `c0699789`; the f2-bump CTC block was left intact.
- No C++ bridge driver files, Golden Gate bridge implementation files, FireMarshal files, or FireSim submodule files were edited.
- The code commit includes one EOF blank-line normalization in `TargetConfigs.scala`; the CTC config body itself was not changed.

## Gate 0 Last 50 Lines

```text
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: AutoCounterFirrtlAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: AutoCounterFirrtlAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: AutoCounterFirrtlAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: AutoCounterFirrtlAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: AutoCounterFirrtlAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: AutoCounterFirrtlAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: AutoCounterFirrtlAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: AutoCounterFirrtlAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: BridgeAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: BridgeAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: BridgeAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: BridgeAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: TriggerSourceAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: TriggerSourceAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: BridgeAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: BridgeAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: GlobalResetCondition
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: FirrtlMemModelAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: FirrtlEnableModelMultiThreadingAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: OutputBaseNameAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: TopModuleAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: ConfigsAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
[WARNING] Unsupported annotation: LegacySFCAnnotation
 Please report this issue at https://github.com/ucb-bar/firrtl2/issues
echo "$mfc_extra_anno_contents" > /scratch/anishs/chipyard/sims/firesim-staging/generated-src/firechip.chip.FireSim.FireSimRocketSSDLatencyConfig/firechip.chip.FireSim.FireSimRocketSSDLatencyConfig.extrafirtool.anno.json
jq -s '[.[][]]' /scratch/anishs/chipyard/sims/firesim-staging/generated-src/firechip.chip.FireSim.FireSimRocketSSDLatencyConfig/firechip.chip.FireSim.FireSimRocketSSDLatencyConfig.anno.json /scratch/anishs/chipyard/sims/firesim-staging/generated-src/firechip.chip.FireSim.FireSimRocketSSDLatencyConfig/firechip.chip.FireSim.FireSimRocketSSDLatencyConfig.extrafirtool.anno.json > /scratch/anishs/chipyard/sims/firesim-staging/generated-src/firechip.chip.FireSim.FireSimRocketSSDLatencyConfig/firechip.chip.FireSim.FireSimRocketSSDLatencyConfig.appended.anno.json
make: Leaving directory '/scratch/anishs/chipyard/sims/firesim-staging'
```

# Phase 4 Step 4 Retry -- Testchipip Pointer Correction

- Timestamp: 2026-05-25T20:27:52-07:00
- Summary: Corrected the `testchipip` superproject pointer after the Step 4 ABI miss, split the contaminated Step 3 status commit, and removed leftover Step 4 candidate edits.
- Diagnostic scenario: SCENARIO X
  - `ce3bab5a` still recorded `generators/testchipip` at `f86a977e9a2d0c7831c8e2ce698ec27e0615a68f`.
  - The amended Step 3 status commit had absorbed the corrected `generators/testchipip` pointer to `204d231ff73e053c97d6933bf5e4296f9ab6236d`.
- Fix option used: OPTION 1
  - Used the low-risk split path because only the current top commit needed repair, and `git branch -r --contains 381da651` showed no remote refs containing the contaminated commit.
  - Replaced the contaminated status commit with a status-only commit, then added a separate pointer correction commit.
- Final `ssd-port-f2` commit log before this documentation commit:

```text
7bbdec07 Bump testchipip to include 256-bit beat widening
e36636a3 Record Phase 4 Step 3 status
0bcb34bf Port SSD config wiring and 256-bit block-device ABI declaration
373b8718 Add port plan to ssd-port-f2 branch
ce3bab5a Bump testchipip for SSD block-device tracker depth (f2 port)
5845cf17 bump firesim with various f2 fixes
```

- Final `testchipip` SHA: `204d231ff73e053c97d6933bf5e4296f9ab6236d`
- `testchipip` local history:

```text
204d231 Allow 256 block-device trackers
c5e7bf3 Widen block device beats to 256 bits
5dca05b Add FastRAM for faster sims with DRAM over serialTL (#271)
```

- Confirmation: `dataBitsPerBeat = 256` in `generators/testchipip/src/main/scala/iceblk/BlockDevice.scala`.
- Confirmation: `require (nTrackers <= 256)` is present in `generators/testchipip/src/main/scala/iceblk/BlockDevice.scala`.
- Confirmation: leftover Step 4 candidate edits were discarded:
  - `generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.cc`
  - `generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.h`
  - `generators/firechip/goldengateimplementations/src/main/scala/BlockDevBridgeModule.scala`
  - `generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc`
  - `generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.h`
- Confirmation: working tree was clean before this documentation update.

# Phase 4 Step 4 Status

- Timestamp: 2026-05-25T20:57:36-07:00
- Summary: Ported the SSD PAL/ICL host-timing model, 256-deep host-side block-device ABI implementation, plusarg parsing, and Golden Gate host-timing bypass; Gate 1 passed, Gate 2 was skipped by instruction, and the corrected F2 FireSim compile passed.
- Step 4 commit: `a6a13d5c Port SSD host-timing model and 256-deep ABI C++ implementation`
- Files changed by Step 4:

```text
generators/firechip/goldengateimplementations/src/main/scala/BlockDevBridgeModule.scala
generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.cc
generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.h
generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc
generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.h
```

- `ssd_latency_model` source inventory:
  - `ssd_latency_model.cc`: 896 lines, byte-exact copy from `SSD`.
  - `ssd_latency_model.h`: 178 lines, byte-exact copy from `SSD`.

## ABI consistency check

`generators/firechip/bridgeinterfaces/src/main/scala/BlockDevice.scala` declares:

```scala
case class BlockDeviceConfig(
  nTrackers: Int = 1,
  reqQueueDepth: Int = 256,
  dataQueueDepth: Int = 256,
  rRespQueueDepth: Int = 256,
  wAckQueueDepth: Int = 256,
)
```

| Parameter | Scala value | C++ / bridge value | Result |
| --- | --- | --- | --- |
| Data beat width | `256` in firechip bridge interface and `testchipip` `BlockDeviceIO` | `BEAT_WORDS = 4`, `uint64_t data[BEAT_WORDS]`, 4 x 64 bits = 256 bits | PASS |
| Tracker count max | `testchipip` permits `nTrackers <= 256`; `FireSimRocketSSDLatencyConfig` uses 256 | `blockdev_t` receives `num_trackers`, stores `_ntags`, and sizes `write_trackers` from `_ntags` | PASS |
| req queue depth | `reqQueueDepth = 256` | `BlockDevBridgeModule` allocates `reqBuf` from `blockDevExternal.reqQueueDepth`; C++ has no conflicting hard-coded depth | PASS |
| data queue depth | `dataQueueDepth = 256` | `BlockDevBridgeModule` allocates `dataBuf` from `blockDevExternal.dataQueueDepth`; C++ has no conflicting hard-coded depth | PASS |
| rResp queue depth | `rRespQueueDepth = 256` | `BlockDevBridgeModule` allocates `rRespBuf` from `blockDevExternal.rRespQueueDepth`; C++ has no conflicting hard-coded depth | PASS |
| wAck queue depth | `wAckQueueDepth = 256` | `BlockDevBridgeModule` allocates `wAckBuf` from `blockDevExternal.wAckQueueDepth`; C++ has no conflicting hard-coded depth | PASS |

Gate 3 log also confirms Golden Gate instantiated `BlockDevBridgeModule` with `BlockDeviceConfig(256,256,256,256,256)`.

## Gate 1 - C++ unit tests

Commands run:

```bash
mkdir -p /tmp/ssd-unit-tests
git show SSD:generators/firechip/bridgestubs/src/main/cc/bridges/test/ssd_latency_model_unit.cc \
  > /tmp/ssd-unit-tests/ssd_latency_model_unit.cc
git show SSD:generators/firechip/bridgestubs/src/main/cc/bridges/test/blockdev_host_timing_unit.cc \
  > /tmp/ssd-unit-tests/blockdev_host_timing_unit.cc

c++ -std=c++17 \
  -I generators/firechip/bridgestubs/src/main/cc \
  -I sims/firesim/sim/midas/src/main/cc \
  /tmp/ssd-unit-tests/ssd_latency_model_unit.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc \
  -o /tmp/ssd-unit-tests/ssd_latency_model_unit
/tmp/ssd-unit-tests/ssd_latency_model_unit

c++ -std=c++17 -DBLOCKDEV_UNIT_TEST \
  -I generators/firechip/bridgestubs/src/main/cc \
  -I sims/firesim/sim/midas/src/main/cc \
  /tmp/ssd-unit-tests/blockdev_host_timing_unit.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/blockdev.cc \
  generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc \
  -o /tmp/ssd-unit-tests/blockdev_host_timing_unit
/tmp/ssd-unit-tests/blockdev_host_timing_unit
```

Results:

- `ssd_latency_model_unit`: compile PASS, run PASS, exit 0.
  - Output included expected negative-config messages:

```text
ssd_latency_model: unsupported cell type: wat
ssd_latency_model: icl_eviction_policy must be "lru"
```

- `blockdev_host_timing_unit`: original compile command without `blockdev.cc` failed at link time with undefined `blockdev_t` references, so it was retried with `blockdev.cc` included. Retry compile PASS, run PASS, exit 0.
  - Output included `ssd_latency_model_config`, `icl_csv_read`, `icl_csv_write`, and the expected completion-heap overflow diagnostic:

```text
Block device completion heap exceeded 1 entries.
```

## Gate 2 - Scala BridgeSuite

- Skipped by instruction for this Step 4 run; no in-tree SSD BridgeSuite port was attempted in this phase.

## Gate 3 - FireSim compile

Attempted F1 command:

```bash
make -C sim compile \
  TARGET_PROJECT=firesim \
  TARGET_PROJECT_MAKEFRAG=../../generators/firechip/chip/src/main/makefrag/firesim \
  TARGET_CONFIG=FireSimRocketSSDLatencyConfig \
  PLATFORM=f1 \
  PLATFORM_CONFIG=BaseF1Config
```

Result: FAIL before compile. This F2-bump FireSim checkout rejects `PLATFORM=f1`:

```text
make/fpga.mk:24: *** Invalid PLATFORM used: f1.  Stop.
```

Attempted F2 command with the originally supplied makefrag path:

```bash
make -C sim compile \
  TARGET_PROJECT=firesim \
  TARGET_PROJECT_MAKEFRAG=../../generators/firechip/chip/src/main/makefrag/firesim \
  TARGET_CONFIG=FireSimRocketSSDLatencyConfig \
  PLATFORM=f2 \
  PLATFORM_CONFIG=BaseF2Config
```

Result: FAIL before compile because `make -C sim` resolves paths from `sims/firesim/sim`, so the makefrag path was one directory short:

```text
Makefile:51: ../../generators/firechip/chip/src/main/makefrag/firesim/config.mk: No such file or directory
make: *** No rule to make target '../../generators/firechip/chip/src/main/makefrag/firesim/config.mk'.  Stop.
```

Corrected F2 command:

```bash
cd /scratch/anishs/chipyard/sims/firesim
source sourceme-manager.sh
make -C sim compile \
  TARGET_PROJECT=firesim \
  TARGET_PROJECT_MAKEFRAG=../../../generators/firechip/chip/src/main/makefrag/firesim \
  TARGET_CONFIG=FireSimRocketSSDLatencyConfig \
  PLATFORM=f2 \
  PLATFORM_CONFIG=BaseF2Config 2>&1 | tee /tmp/gate3-f2-retry.log
```

Result: PASS, exit 0. The command emitted F2 Golden Gate outputs under:

```text
sims/firesim/sim/generated-src/f2/f2-firesim-FireSim-FireSimRocketSSDLatencyConfig-BaseF2Config/
```

Relevant log excerpt:

```text
Instantiating bridge ep of type firechip.goldengateimplementations.BlockDevBridgeModule
  With constructor arguments: BlockDeviceConfig(256,256,256,256,256)
Simulator Memory Map:
  [   0,   ff]: BlockDevBridgeModule_0
QSFP bits at FPGATop 256
make: Leaving directory '/scratch/anishs/chipyard/sims/firesim/sim'
```

Note: `source sourceme-manager.sh` reported a missing `/home/eecs/anishs/firesim.pem`, but that warning did not prevent the local compile target from completing.
