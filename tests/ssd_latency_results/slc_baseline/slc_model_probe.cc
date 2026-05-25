// Host-side model probe that replays the SLC pal-parallelism contiguous-read
// batch through the C++ SsdLatencyModel and prints the predicted host-release
// cycle count. The metasim measurement is in
// tests/ssd_latency_results/pal_read_write_compare.csv (read,contiguous_cycles
// = 6304 for the Channel=8,Die=1 SLC baseline). The gap between that
// measurement and the prediction emitted here is the per-batch CPU-MMIO drain
// time (see context Section 6 and Section 13).

#include "bridges/ssd_latency_model.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static std::string slc_baseline_ini() {
  return std::string(
      "[sim]\n"
      "FixedPolicy=false\n"
      "TargetClockHz=1000000000\n"
      "\n"
      "[pal]\n"
      "Channel=8\n"
      "Way=1\n"
      "Die=1\n"
      "Plane=1\n"
      "PageSize=512\n"
      "DMASpeed=512000000000\n"
      "NANDType=SLC\n"
      "Read.LSB=5000\n"
      "Program.LSB=25000\n"
      "\n"
      "[hil]\n"
      "CmdOverhead=2\n"
      "CompletionOverhead=3\n"
      "HostBandwidth=512000000000\n");
}

static void write_ini(const std::string &path, const std::string &body) {
  std::ofstream out(path);
  out << body;
}

int main(int argc, char **argv) {
  const char *ini_path = "/tmp/slc_baseline_probe.ini";
  if (argc > 1) {
    ini_path = argv[1];
    SsdLatencyModel m;
    m.load_config(ini_path);
  } else {
    write_ini(ini_path, slc_baseline_ini());
  }

  SsdLatencyModel model;
  model.load_config(ini_path);

  // Mirror run_read_batch(contiguous_offsets) in blkdev-pal-parallelism.c:
  // 8 single-sector reads at sector offsets 0..7. The benchmark issues them
  // sequentially in a tight loop; we approximate the issue cadence as one
  // arrival cycle apart (one MMIO write per request, lower-bound). The model
  // is deterministic for any non-decreasing arrival sequence, and the
  // host-release cycle of the slowest request dominates regardless.
  uint64_t host_complete_max = 0;
  for (uint32_t i = 0; i < 8; i++) {
    SsdRequest req;
    req.write = false;
    req.offset = i;
    req.len = 1;
    req.tag = i;
    const uint64_t completion = model.submit(req, /*t_arrival=*/i);
    if (completion > host_complete_max) {
      host_complete_max = completion;
    }
  }

  // Predicted "batch host-release cycles" = host_complete of slowest request
  // minus the t=0 start. The benchmark's rdcycle bracket starts before the
  // first send and ends after wait_for_completions returns.
  std::printf("slc_baseline_contiguous_read_predicted_cycles %lu\n",
              static_cast<unsigned long>(host_complete_max));
  std::printf("slc_baseline_total_beats 128\n");
  std::printf("slc_baseline_measured_cycles 6304\n");
  std::printf("slc_baseline_delta_cycles %ld\n",
              static_cast<long>(6304L - static_cast<long>(host_complete_max)));
  std::printf("slc_baseline_cycles_per_mmio_beat %.4f\n",
              (6304.0 - static_cast<double>(host_complete_max)) / 128.0);
  return 0;
}
