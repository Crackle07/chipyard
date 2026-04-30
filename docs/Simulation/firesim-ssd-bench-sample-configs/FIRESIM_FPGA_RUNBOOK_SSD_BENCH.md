# FireSim SSD Benchmark FPGA Runbook

These notes describe the tracked sample setup for running the `br-ssd-bench`
FireMarshal workload on an FPGA-backed FireSim deployment.

The files in this directory are samples, not live manager state. Copy or merge
them into `sims/firesim/deploy/` on the manager host after deciding which
platform, hardware DB entry, and workload image paths are correct for that host.

## Files In This Sample

- `config_runtime.yaml`: one-node runtime config for `br-ssd-bench.json`.
- `config_hwdb.yaml`: placeholder hardware DB entry for the SSD-capable Rocket design.
- `config_build_recipes.yaml`: build recipe scaffold for `FireSimRocketConfig`.
- `config_build.yaml`: build selection scaffold with the SSD build disabled by default.
- `workloads/br-ssd-bench.json`: sanitized workload entry generated from the FireMarshal workload.

## Current SSD Workload Shape

The FireMarshal workload lives at:

```sh
software/firemarshal/boards/firechip/base-workloads/br-ssd-bench.json
```

It inherits the firechip `br-base` workload and adds:

- `/root/blkdev_bench.c`, `/root/bench_linux.c`, `blkdev.h`, and `mmio.h`
- `/root/run_bench.sh`
- boot-time init scripts under `overlay/etc/init.d/`
- FireSim start/end trigger binaries built from `trigger/start.S` and `trigger/end.S`

The benchmark binaries are generated during FireMarshal `host-init`; generated
ELFs are not tracked in git.

## Build The FireMarshal Workload

From the FireMarshal submodule:

```sh
cd /path/to/chipyard/software/firemarshal
./marshal -v build boards/firechip/base-workloads/br-ssd-bench.json
./marshal install boards/firechip/base-workloads/br-ssd-bench.json
```

Expected outputs:

- `images/firechip/br-ssd-bench/br-ssd-bench.img`
- `images/firechip/br-ssd-bench/br-ssd-bench-bin`
- `../sims/firesim/deploy/workloads/br-ssd-bench.json`

If `marshal install` rewrites the workload JSON, keep the important outputs:

```json
{
  "benchmark_name": "br-ssd-bench",
  "common_simulation_outputs": ["uartlog", "blkdev-log0"],
  "common_outputs": ["/tmp/ssd_bench.log", "/root/blkdev_bench_out.log"]
}
```

## Configure FireSim Deploy

On the manager host:

```sh
SAMPLE_DIR=/path/to/chipyard/docs/Simulation/firesim-ssd-bench-sample-configs
cd /path/to/chipyard/sims/firesim/deploy
cp "$SAMPLE_DIR/config_runtime.yaml" config_runtime.yaml
cp "$SAMPLE_DIR/config_hwdb.yaml" config_hwdb.yaml
cp "$SAMPLE_DIR/config_build_recipes.yaml" config_build_recipes.yaml
cp "$SAMPLE_DIR/config_build.yaml" config_build.yaml
cp "$SAMPLE_DIR/workloads/br-ssd-bench.json" workloads/br-ssd-bench.json
```

Then edit the live copies:

- Fill in `config_hwdb.yaml` with either an AGFI/AFI entry or a local bitstream tarball.
- Adjust `PLATFORM`, `PLATFORM_CONFIG`, and `bit_builder_recipe` if the manager is not using F1.
- Confirm `common_bootbinary` and `common_rootfs` point at the installed FireMarshal image.
- Add future latency-model plusargs only after the C++ latency model has a stable config interface.

The sample runtime keeps `plusarg_passthrough` limited to target frequency:

```yaml
plusarg_passthrough: "+blkdev-target-freq-mhz0=1000"
```

A future latency model can add its own plusarg here. The tracked sample
intentionally does not pass any external flash-model config file.

## Build Or Select Hardware

If a matching SSD-capable hardware image already exists, add it to the live
`config_hwdb.yaml` entry named `firesim_rocket_singlecore_ssd_bench`.

To build from the sample recipe, first enable the build in the live
`config_build.yaml`:

```yaml
builds_to_run:
  - firesim_rocket_singlecore_ssd_bench
```

Then run:

```sh
firesim buildbitstream
```

When the build completes, copy the resulting artifact details into the live
`config_hwdb.yaml`.

## Launch The FPGA Run

Standard FireSim flow:

```sh
cd /path/to/chipyard/sims/firesim/deploy
firesim infrasetup
firesim runworkload
firesim terminaterunfarm
```

The workload should invoke `/usr/bin/firesim-start-trigger` before the benchmark
region and `/usr/bin/firesim-end-trigger` at the end, allowing
`terminate_on_completion: yes` to stop the run.

## Collect Results

FireSim copies results under:

```sh
sims/firesim/deploy/results-workload/<timestamp>/
```

Useful files:

- `uartlog`: kernel and benchmark console output
- `/tmp/ssd_bench.log`: wrapper output from `run_bench.sh`
- `/root/blkdev_bench_out.log`: target-side block-device benchmark log
- `blkdev-log0`: host bridge block-device request log

Quick check:

```sh
grep 'BENCH:' results-workload/*/br-ssd-bench/*/uartlog
```

## Notes

- Linux workloads are meant for FPGA-backed runs; full Linux boot in metasim is
  usually too slow for day-to-day SSD benchmarking.
- Keep live manager configs out of commits. Track reproducible samples here and
  record manager-specific paths or AGFIs only in local deploy state.
