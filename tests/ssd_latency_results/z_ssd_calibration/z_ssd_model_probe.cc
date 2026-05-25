#include "bridges/ssd_latency_model.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

const char *cell_type_name(SsdCellType type) {
  switch (type) {
  case SsdCellType::SLC:
    return "SLC";
  case SsdCellType::MLC:
    return "MLC";
  case SsdCellType::TLC:
    return "TLC";
  case SsdCellType::QLC:
    return "QLC";
  }
  return "UNKNOWN";
}

double cycles_to_us(uint64_t cycles, uint64_t target_clock_hz) {
  return static_cast<double>(cycles) * 1000000.0 /
         static_cast<double>(target_clock_hz);
}

double cycles_to_seconds(uint64_t cycles, uint64_t target_clock_hz) {
  return static_cast<double>(cycles) / static_cast<double>(target_clock_hz);
}

uint64_t first_read_cycles(const char *config_path) {
  SsdLatencyModel model;
  model.load_config(config_path);

  SsdRequest req;
  req.write = false;
  req.offset = 0;
  req.len = 8;
  req.tag = 0;
  return model.submit(req, 0);
}

double random_read_iops(const char *config_path, uint64_t *elapsed_cycles) {
  SsdLatencyModel model;
  model.load_config(config_path);
  const SsdLatencyConfig &cfg = model.config();

  const uint32_t request_count = 4096;
  const uint32_t page_sectors = cfg.page_bytes / 512;
  uint64_t latest = 0;
  for (uint32_t i = 0; i < request_count; i++) {
    SsdRequest req;
    req.write = false;
    req.offset = i * page_sectors;
    req.len = 8;
    req.tag = i & 0xff;
    const uint64_t done = model.submit(req, 0);
    if (done > latest) {
      latest = done;
    }
  }

  *elapsed_cycles = latest;
  return static_cast<double>(request_count) /
         cycles_to_seconds(latest, cfg.target_clock_hz);
}

double sequential_read_gbps(const char *config_path, uint64_t *elapsed_cycles) {
  SsdLatencyModel model;
  model.load_config(config_path);
  const SsdLatencyConfig &cfg = model.config();

  const uint64_t total_bytes = 256ULL * 1024ULL * 1024ULL;
  const uint32_t request_bytes = cfg.page_bytes;
  const uint32_t request_sectors = request_bytes / 512;
  const uint32_t request_count = total_bytes / request_bytes;

  uint64_t latest = 0;
  for (uint32_t i = 0; i < request_count; i++) {
    SsdRequest req;
    req.write = false;
    req.offset = i * request_sectors;
    req.len = request_sectors;
    req.tag = i & 0xff;
    const uint64_t done = model.submit(req, 0);
    if (done > latest) {
      latest = done;
    }
  }

  *elapsed_cycles = latest;
  const double seconds = cycles_to_seconds(latest, cfg.target_clock_hz);
  return static_cast<double>(total_bytes) / seconds / 1000000000.0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s CONFIG.ini\n", argv[0]);
    return 2;
  }

  const char *config_path = argv[1];
  SsdLatencyModel model;
  model.load_config(config_path);
  const SsdLatencyConfig &cfg = model.config();

  const uint64_t first_cycles = first_read_cycles(config_path);
  uint64_t random_cycles = 0;
  uint64_t seq_cycles = 0;
  const double iops = random_read_iops(config_path, &random_cycles);
  const double seq_gbps = sequential_read_gbps(config_path, &seq_cycles);

  std::printf("date,source,benchmark,config,target_clock_hz,cell_type,"
              "n_channels,n_dies_per_channel,n_planes,page_size_bytes,"
              "bus_speed_mtps,host_overhead_cycles,wb_ack_latency_cycles,"
              "target_read_us_min,target_read_us_max,first_read_us,"
              "target_random_read_iops_min,target_random_read_iops_max,"
              "random_read_iops,target_seq_read_gbps_min,seq_read_gbps,"
              "first_read_cycles,random_elapsed_cycles,seq_elapsed_cycles,"
              "notes\n");
  std::printf("2026-05-13,assistant_model_probe,ssd_latency_model,%s,%lu,%s,"
              "%u,%u,%u,%u,%u,%lu,%lu,12,20,%.3f,600000,900000,%.0f,"
              "2.5,%.3f,%lu,%lu,%lu,"
              "\"Host-side C++ PAL probe for Samsung SZ985 Z-SSD preset; "
              "4096 outstanding 4KB page-aligned reads and 256MiB "
              "page-sized sequential reads\"\n",
              config_path,
              static_cast<unsigned long>(cfg.target_clock_hz),
              cell_type_name(cfg.cell_type),
              cfg.channels,
              cfg.dies,
              cfg.planes,
              cfg.page_bytes,
              cfg.bus_speed_mtps,
              static_cast<unsigned long>(cfg.host_overhead_cycles),
              static_cast<unsigned long>(cfg.wb_ack_latency_cycles),
              cycles_to_us(first_cycles, cfg.target_clock_hz),
              iops,
              seq_gbps,
              static_cast<unsigned long>(first_cycles),
              static_cast<unsigned long>(random_cycles),
              static_cast<unsigned long>(seq_cycles));
  return 0;
}
