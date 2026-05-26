#ifndef __SSD_LATENCY_MODEL_H
#define __SSD_LATENCY_MODEL_H

#include <cstdint>
#include <functional>
#include <list>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class SsdCellType {
  SLC,
  MLC,
  TLC,
  QLC,
};

enum class PageType {
  SINGLE,
  LSB,
  CSB,
  MSB,
};

struct SsdRequest {
  bool write = false;
  uint32_t offset = 0;
  uint32_t len = 0;
  uint32_t tag = 0;
};

struct SsdSubmitResult {
  // Sentinel for SsdSubmitResult::icl_victim_lpn when no eviction occurred.
  static constexpr uint64_t kNoVictim = static_cast<uint64_t>(-1);

  uint64_t host_complete_cycle = 0;
  uint64_t nand_complete_cycle = 0;
  bool write_ack_before_nand = false;
  bool writeback_scheduled = false;

  uint64_t icl_occupancy_lines = 0;
  uint64_t icl_dirty_lines = 0;
  uint64_t icl_evictions_total = 0;
  uint64_t icl_writeback_bytes_total = 0;
  bool icl_was_hit = false;
  bool icl_eviction_triggered = false;
  uint64_t icl_writeback_latency_cycles = 0;
  uint64_t icl_victim_lpn = kNoVictim;
  bool icl_was_read_hit = false;
  bool icl_rmw_triggered = false;
  uint64_t icl_rmw_fetch_cycles = 0;
};

struct SsdLatencyConfig {
  bool fixed_policy = true;

  uint64_t target_clock_hz = 1000000000ULL;

  uint32_t channels = 1;
  uint32_t ways = 1;
  uint32_t dies = 1;
  uint32_t planes = 1;
  uint32_t pages_per_block = 1024;
  uint32_t page_bytes = 4096;
  uint32_t bits_per_cell = 1;
  uint32_t n_meta_pages = 8;
  uint32_t bus_speed_mtps = 0;
  SsdCellType cell_type = SsdCellType::SLC;

  double dma_bytes_per_sec = 1000000000.0;
  uint64_t read_lsb_cycles = 1;
  uint64_t read_csb_cycles = 1;
  uint64_t read_msb_cycles = 1;
  uint64_t program_lsb_cycles = 1;
  uint64_t program_csb_cycles = 1;
  uint64_t program_msb_cycles = 1;
  uint64_t erase_cycles = 0;

  uint64_t cmd_overhead_cycles = 0;
  uint64_t completion_overhead_cycles = 0;
  uint64_t host_overhead_cycles = 0;
  double host_bytes_per_sec = 1000000000.0;

  bool wb_enabled = false;
  uint64_t wb_ack_latency_cycles = 0;

  uint64_t icl_capacity_bytes = 0;
  uint32_t icl_line_size_bytes = 0;
  std::string icl_eviction_policy = "lru";
  std::string icl_associativity = "full";
};

class SsdLatencyModel {
public:
  SsdLatencyModel();

  void load_config(const std::string &ini_path);
  void reset();
  void tick(uint64_t t_target_cycle);

  uint64_t submit(const SsdRequest &req, uint64_t t_arrival);
  SsdSubmitResult submit_timing(const SsdRequest &req, uint64_t t_arrival);
  uint64_t peek(const SsdRequest &req, uint64_t t_arrival) const;

  bool fixed_policy() const { return cfg_.fixed_policy; }
  const SsdLatencyConfig &config() const { return cfg_; }
  PageType page_type(uint64_t ppn) const;
  uint64_t pending_count() const { return pending_nand_completions_.size(); }
  uint64_t icl_occupancy_lines() const;
  uint64_t icl_dirty_lines() const;
  uint64_t icl_evictions_total() const;
  uint64_t icl_writeback_bytes_total() const;

private:
  struct Address {
    uint32_t channel;
    uint32_t way;
    uint32_t die;
    uint32_t plane;
  };

  struct WriteBufferLine {
    uint64_t lpn = 0;
    bool dirty = false;
    uint64_t insert_cycle = 0;
    uint64_t last_access_cycle = 0;
  };

  struct WriteBuffer {
    uint64_t capacity_lines = 0;
    uint64_t dirty_lines = 0;
    uint64_t evictions_total = 0;
    uint64_t writeback_bytes_total = 0;
    std::unordered_map<uint64_t, WriteBufferLine> lines;
    std::list<uint64_t> lru;
  };

  SsdLatencyConfig cfg_;
  WriteBuffer write_buffer_;
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>> channel_reservations_;
  std::vector<std::pair<uint64_t, uint64_t>> host_reservations_;
  std::vector<uint64_t> die_busy_until_;
  std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>>
      pending_nand_completions_;

  void configure_resources();
  Address map_lpn(uint64_t lpn) const;
  void touch_write_buffer_line(uint64_t lpn, uint64_t t_access);
  void insert_write_buffer_line(uint64_t lpn, uint64_t t_access);
  uint64_t evict_write_buffer_line(uint64_t t_arrival, uint64_t *victim_lpn);
  SsdSubmitResult submit_write_buffered(uint64_t t_arrival,
                                        uint64_t start_byte,
                                        uint64_t bytes,
                                        uint64_t first_lpn,
                                        uint64_t last_lpn);
  bool icl_read_all_hit(uint64_t first_lpn, uint64_t last_lpn) const;
  SsdSubmitResult submit_read_cache_hit(uint64_t t_arrival,
                                        uint64_t first_lpn,
                                        uint64_t last_lpn);
  uint64_t reserve_channel(uint32_t channel,
                           uint64_t earliest,
                           uint64_t cycles);
  uint64_t reserve_host(uint64_t earliest, uint64_t cycles);
  uint64_t schedule_page_op(const Address &addr,
                            uint64_t ppn,
                            bool write,
                            uint64_t bytes,
                            uint64_t t_arrival);
  uint64_t read_cycles(PageType type) const;
  uint64_t program_cycles(PageType type) const;
  uint64_t transfer_cycles(uint64_t bytes, double bytes_per_sec) const;
  uint64_t ns_to_cycles(uint64_t ns) const;
  uint64_t die_index(const Address &addr) const;
};

#endif // __SSD_LATENCY_MODEL_H
