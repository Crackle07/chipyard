# SLC baseline host-model prediction

Used by `tests/verify_pm1725a_metasim.py` as the regression-only cross-check
for the calibrated `CYCLES_PER_MMIO_BEAT` / `PER_REQUEST_DRAIN_OVERHEAD_CYCLES`
constants. See the docstring on those constants for the derivation; this
directory keeps the inputs reproducible.

Files

- `slc_model_probe.cc` — host-side probe that loads the SLC pal-parallelism
  baseline INI (Channel=8, Die=1, NANDType=SLC, Read.LSB=5000 ns) into the
  C++ `SsdLatencyModel` and replays the 8 single-sector contiguous-read submits
  performed by `tests/blkdev-pal-parallelism.c::run_read_batch(contiguous_offsets)`.
- `slc_model_prediction.txt` — captured stdout of `slc_model_probe`. The
  predicted host-release cycle count is `5015`; the measured
  `read_contiguous_cycles` from
  `tests/ssd_latency_results/pal_read_write_compare.csv` is `6304` (gap 1289
  cycles over 128 total bridge beats).

Build

```
g++ -std=c++17 -O2 -Wall -Wextra \
    -I$(chipyard)/generators/firechip/bridgestubs/src/main/cc \
    slc_model_probe.cc \
    $(chipyard)/generators/firechip/bridgestubs/src/main/cc/bridges/ssd_latency_model.cc \
    -o slc_model_probe
```

Why this is a cross-check and not the calibration source

The SLC pal-parallelism rdcycle bracket spans 8 issue-side `blkdev_send_request`
MMIO writes plus 8 per-request completion handshakes, none of which are inside
the PM1725a probes' brackets. Its residual (1289 cycles) therefore mixes
per-request issue cost with the same per-beat drain that the PM1725a probes
isolate cleanly. Calibrating from SLC alone would yield D ≈ 10.07 cyc/beat,
which over-predicts the PM1725a probes by ~190 cycles. The verifier instead
calibrates jointly from the two PM1725a probes (1-sector and 8-sector) and
uses the SLC number purely as a regression check on the underlying physical
picture.
