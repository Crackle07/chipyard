# SSD latency model metasim results

Captured on 2026-05-12 from the baremetal block-device PAL benchmarks.

Common config unless a CSV overrides it:

- TargetClockHz: 1000000000
- PageSize: 512
- Way: 1
- Plane: 1
- DMASpeed: 512000000000
- HostBandwidth: 512000000000
- CmdOverhead: 2
- CompletionOverhead: 3
- NANDType: SLC

CSV files:

- `pal_channel_sweep.csv`: stride-8 versus contiguous reads while increasing `Channel`.
- `pal_die_sweep.csv`: stride-8 versus contiguous reads while increasing `Die` with one channel.
- `pal_read_latency_sweep.csv`: same 8-channel experiment while increasing `Read.LSB`.
- `pal_read_write_compare.csv`: read and write PAL ratios from the combined benchmark.
- `qdepth_sweep.csv`: read queue-depth scaling results.
- `pm1725a_model_calibration.csv`: host-side C++ PAL calibration probe for the modern TLC PM1725a preset.
- `z_ssd_calibration/z_ssd_model_calibration.csv`: host-side C++ PAL calibration probe for the Samsung SZ985 Z-SSD preset.
- `pm1725a_metasim_probe*/comparison.txt` and `z_ssd_metasim_probe*/comparison.txt`: single-request metasim checks against the host-side model plus target-side response-drain constants.

For the PAL benchmark, `stride8_cycles` means requests at sectors `0,8,16,...,56`; with `Channel=8,Die=1` this maps them all to channel 0. `contiguous_cycles` means sectors `0..7`; with `Channel=8,Die=1` this maps one request per channel.
