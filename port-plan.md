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
