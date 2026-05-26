#include "bridges/ssd_latency_model.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>

namespace {

static std::string trim(const std::string &s) {
  const char *spaces = " \t\r\n";
  const std::string::size_type first = s.find_first_not_of(spaces);
  if (first == std::string::npos) {
    return "";
  }
  const std::string::size_type last = s.find_last_not_of(spaces);
  return s.substr(first, last - first + 1);
}

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static void die_config(const std::string &msg) {
  std::fprintf(stderr, "ssd_latency_model: %s\n", msg.c_str());
  std::exit(1);
}

static uint64_t parse_u64(const std::string &value, const std::string &key) {
  errno = 0;
  char *end = nullptr;
  const unsigned long long parsed =
      std::strtoull(value.c_str(), &end, 0);
  if (errno != 0 || end == value.c_str() || trim(end) != "") {
    die_config("invalid integer for " + key + ": " + value);
  }
  return static_cast<uint64_t>(parsed);
}

static double parse_double(const std::string &value, const std::string &key) {
  errno = 0;
  char *end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  if (errno != 0 || end == value.c_str() || trim(end) != "" ||
      parsed <= 0.0) {
    die_config("invalid positive number for " + key + ": " + value);
  }
  return parsed;
}

static bool parse_bool(const std::string &value, const std::string &key) {
  const std::string v = lower(trim(value));
  if (v == "1" || v == "true" || v == "yes" || v == "on") {
    return true;
  }
  if (v == "0" || v == "false" || v == "no" || v == "off") {
    return false;
  }
  die_config("invalid boolean for " + key + ": " + value);
  return false;
}

using Ini = std::map<std::string, std::map<std::string, std::string>>;
using KeyRef = std::pair<std::string, std::string>;

static const char *kGlobalSection = "ssd";

static Ini parse_ini(const std::string &path) {
  std::ifstream input(path.c_str());
  if (!input) {
    die_config("could not open config " + path);
  }

  Ini ini;
  std::string section;
  std::string line;
  unsigned line_no = 0;
  while (std::getline(input, line)) {
    line_no++;
    const std::string::size_type comment = line.find_first_of("#;");
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = lower(trim(line.substr(1, line.size() - 2)));
      if (section.empty()) {
        die_config("empty section at line " + std::to_string(line_no));
      }
      continue;
    }
    const std::string::size_type eq = line.find('=');
    if (eq == std::string::npos) {
      die_config("expected key=value at line " + std::to_string(line_no));
    }
    if (section.empty()) {
      section = kGlobalSection;
    }
    const std::string key = lower(trim(line.substr(0, eq)));
    const std::string value = trim(line.substr(eq + 1));
    if (key.empty()) {
      die_config("empty key at line " + std::to_string(line_no));
    }
    ini[section][key] = value;
  }
  return ini;
}

static bool find_value(const Ini &ini,
                       const std::vector<KeyRef> &keys,
                       std::string *value) {
  for (const auto &key : keys) {
    const Ini::const_iterator sec = ini.find(key.first);
    if (sec == ini.end()) {
      continue;
    }
    const std::map<std::string, std::string>::const_iterator it =
        sec->second.find(key.second);
    if (it != sec->second.end()) {
      *value = it->second;
      return true;
    }
  }
  return false;
}

static bool has_any(const Ini &ini, const std::vector<KeyRef> &keys) {
  std::string ignored;
  return find_value(ini, keys, &ignored);
}

static uint64_t get_u64_any(const Ini &ini,
                            const std::vector<KeyRef> &keys,
                            uint64_t default_value) {
  std::string value;
  if (!find_value(ini, keys, &value)) {
    return default_value;
  }
  return parse_u64(value, keys.front().first + "." + keys.front().second);
}

static double get_double_any(const Ini &ini,
                             const std::vector<KeyRef> &keys,
                             double default_value) {
  std::string value;
  if (!find_value(ini, keys, &value)) {
    return default_value;
  }
  return parse_double(value, keys.front().first + "." + keys.front().second);
}

static bool get_bool_any(const Ini &ini,
                         const std::vector<KeyRef> &keys,
                         bool default_value) {
  std::string value;
  if (!find_value(ini, keys, &value)) {
    return default_value;
  }
  return parse_bool(value, keys.front().first + "." + keys.front().second);
}

static std::string get_string_any(const Ini &ini,
                                  const std::vector<KeyRef> &keys,
                                  const std::string &default_value) {
  std::string value;
  if (!find_value(ini, keys, &value)) {
    return default_value;
  }
  return value;
}

static SsdCellType parse_cell_type(const std::string &value) {
  const std::string v = lower(trim(value));
  if (v == "slc") {
    return SsdCellType::SLC;
  }
  if (v == "mlc") {
    return SsdCellType::MLC;
  }
  if (v == "tlc") {
    return SsdCellType::TLC;
  }
  if (v == "qlc") {
    return SsdCellType::QLC;
  }
  die_config("unsupported cell type: " + value);
  return SsdCellType::SLC;
}

static uint32_t default_bits_per_cell(SsdCellType type) {
  switch (type) {
    case SsdCellType::SLC:
      return 1;
    case SsdCellType::MLC:
      return 2;
    case SsdCellType::TLC:
      return 3;
    case SsdCellType::QLC:
      return 4;
  }
  return 1;
}

static double bus_mtps_to_bytes_per_sec(uint64_t mtps) {
  return static_cast<double>(mtps) * 1000000.0;
}

} // namespace

SsdLatencyModel::SsdLatencyModel() { configure_resources(); }

void SsdLatencyModel::load_config(const std::string &ini_path) {
  cfg_ = SsdLatencyConfig();
  const Ini ini = parse_ini(ini_path);

  cfg_.fixed_policy = get_bool_any(
      ini,
      {{"sim", "fixedpolicy"}, {kGlobalSection, "fixed_policy"},
       {kGlobalSection, "fixedpolicy"}},
      true);
  cfg_.target_clock_hz = get_u64_any(
      ini,
      {{"sim", "targetclockhz"}, {kGlobalSection, "target_clock_hz"},
       {kGlobalSection, "targetclockhz"}},
      cfg_.target_clock_hz);

  std::string cell_type_value;
  if (find_value(ini,
                 {{kGlobalSection, "cell_type"}, {kGlobalSection, "nandtype"},
                  {"pal", "nandtype"}},
                 &cell_type_value)) {
    cfg_.cell_type = parse_cell_type(cell_type_value);
  }

  if (cfg_.cell_type == SsdCellType::TLC) {
    cfg_.page_bytes = 16384;
    cfg_.bus_speed_mtps = 533;
    cfg_.dma_bytes_per_sec = bus_mtps_to_bytes_per_sec(cfg_.bus_speed_mtps);
    cfg_.read_lsb_cycles = ns_to_cycles(77000);
    cfg_.read_csb_cycles = ns_to_cycles(120000);
    cfg_.read_msb_cycles = ns_to_cycles(140000);
    // Micron publishes collapsed typical/max program times. The LSB/CSB/MSB
    // split follows the SimpleSSD TLC page-type ratios, not a Micron table.
    cfg_.program_lsb_cycles = ns_to_cycles(700000);
    cfg_.program_csb_cycles = ns_to_cycles(1300000);
    cfg_.program_msb_cycles = ns_to_cycles(2500000);
    cfg_.erase_cycles = ns_to_cycles(15000000);
  }

  cfg_.bits_per_cell = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"pal", "bitspercell"}, {kGlobalSection, "bits_per_cell"},
       {kGlobalSection, "bitspercell"}},
      default_bits_per_cell(cfg_.cell_type)));

  cfg_.channels = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"pal", "channel"}, {kGlobalSection, "n_channels"},
       {kGlobalSection, "channels"}},
      cfg_.channels));
  cfg_.ways = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"pal", "way"}, {kGlobalSection, "n_ways"}, {kGlobalSection, "ways"}},
      cfg_.ways));
  cfg_.dies = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"pal", "die"}, {kGlobalSection, "n_dies_per_channel"},
       {kGlobalSection, "n_dies"}, {kGlobalSection, "dies"}},
      cfg_.dies));
  cfg_.planes = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"pal", "plane"}, {kGlobalSection, "n_planes"},
       {kGlobalSection, "planes"}},
      cfg_.planes));
  cfg_.pages_per_block = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"pal", "pagesperblock"}, {kGlobalSection, "pages_per_block"},
       {kGlobalSection, "pagesperblock"}},
      cfg_.pages_per_block));
  cfg_.page_bytes = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"pal", "pagesize"}, {kGlobalSection, "page_size_bytes"},
       {kGlobalSection, "pagesize"}},
      cfg_.page_bytes));

  if (has_any(ini, {{"pal", "busspeedmtps"}, {kGlobalSection, "bus_speed_mtps"},
                    {kGlobalSection, "busspeedmtps"}})) {
    cfg_.bus_speed_mtps = static_cast<uint32_t>(get_u64_any(
        ini,
        {{"pal", "busspeedmtps"}, {kGlobalSection, "bus_speed_mtps"},
         {kGlobalSection, "busspeedmtps"}},
        cfg_.bus_speed_mtps));
    cfg_.dma_bytes_per_sec = bus_mtps_to_bytes_per_sec(cfg_.bus_speed_mtps);
  }
  cfg_.dma_bytes_per_sec = get_double_any(
      ini,
      {{"pal", "dmaspeed"}, {kGlobalSection, "dma_speed_bps"},
       {kGlobalSection, "dma_bytes_per_sec"}},
      cfg_.dma_bytes_per_sec);
  cfg_.n_meta_pages = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"pal", "nmetapages"}, {kGlobalSection, "n_meta_pages"},
       {kGlobalSection, "nmetapages"}},
      cfg_.n_meta_pages));

  if (cfg_.channels == 0 || cfg_.ways == 0 || cfg_.dies == 0 ||
      cfg_.planes == 0 || cfg_.pages_per_block == 0 ||
      cfg_.page_bytes == 0 || cfg_.target_clock_hz == 0 ||
      cfg_.bits_per_cell == 0) {
    die_config("Channel, Way, Die, Plane, PageSize, PagesPerBlock, "
               "BitsPerCell, and TargetClockHz must be nonzero");
  }
  if (cfg_.bits_per_cell > 4) {
    die_config("BitsPerCell must be in the range 1..4");
  }
  if (cfg_.n_meta_pages >= cfg_.pages_per_block) {
    die_config("n_meta_pages must be smaller than pages_per_block");
  }

  const auto ns_cycles = [this, &ini](const std::vector<KeyRef> &keys,
                                      uint64_t default_cycles) {
    std::string value;
    if (!find_value(ini, keys, &value)) {
      return default_cycles;
    }
    return ns_to_cycles(
        parse_u64(value, keys.front().first + "." + keys.front().second));
  };

  cfg_.read_lsb_cycles =
      ns_cycles({{"pal", "read.lsb"}, {kGlobalSection, "tr_lsb_ns"},
                 {kGlobalSection, "read_lsb_ns"}},
                cfg_.read_lsb_cycles);
  cfg_.read_csb_cycles =
      ns_cycles({{"pal", "read.csb"}, {kGlobalSection, "tr_csb_ns"},
                 {kGlobalSection, "read_csb_ns"}},
                cfg_.read_csb_cycles);
  cfg_.read_msb_cycles =
      ns_cycles({{"pal", "read.msb"}, {kGlobalSection, "tr_msb_ns"},
                 {kGlobalSection, "read_msb_ns"}},
                cfg_.read_msb_cycles);
  cfg_.program_lsb_cycles =
      ns_cycles({{"pal", "program.lsb"}, {kGlobalSection, "tprog_lsb_ns"},
                 {kGlobalSection, "program_lsb_ns"}},
                cfg_.program_lsb_cycles);
  cfg_.program_csb_cycles =
      ns_cycles({{"pal", "program.csb"}, {kGlobalSection, "tprog_csb_ns"},
                 {kGlobalSection, "program_csb_ns"}},
                cfg_.program_csb_cycles);
  cfg_.program_msb_cycles =
      ns_cycles({{"pal", "program.msb"}, {kGlobalSection, "tprog_msb_ns"},
                 {kGlobalSection, "program_msb_ns"}},
                cfg_.program_msb_cycles);
  cfg_.erase_cycles =
      ns_cycles({{"pal", "erase"}, {kGlobalSection, "tbers_ns"},
                 {kGlobalSection, "erase_ns"}},
                cfg_.erase_cycles);

  if (cfg_.cell_type == SsdCellType::SLC) {
    cfg_.read_csb_cycles = cfg_.read_lsb_cycles;
    cfg_.read_msb_cycles = cfg_.read_lsb_cycles;
    cfg_.program_csb_cycles = cfg_.program_lsb_cycles;
    cfg_.program_msb_cycles = cfg_.program_lsb_cycles;
  }

  cfg_.cmd_overhead_cycles =
      ns_cycles({{"hil", "cmdoverhead"}, {kGlobalSection, "cmd_overhead_ns"}},
                0);
  cfg_.completion_overhead_cycles =
      ns_cycles({{"hil", "completionoverhead"},
                 {kGlobalSection, "completion_overhead_ns"}},
                0);
  cfg_.host_overhead_cycles = get_u64_any(
      ini,
      {{"hil", "hostoverheadcycles"}, {kGlobalSection, "host_overhead_cycles"},
       {kGlobalSection, "hostoverheadcycles"}},
      cfg_.host_overhead_cycles);
  cfg_.host_bytes_per_sec = get_double_any(
      ini,
      {{"hil", "hostbandwidth"}, {kGlobalSection, "host_bandwidth_bps"},
       {kGlobalSection, "host_bytes_per_sec"}},
      cfg_.host_bytes_per_sec);
  cfg_.wb_enabled = get_bool_any(
      ini,
      {{"hil", "wbenabled"}, {kGlobalSection, "wb_enabled"},
       {kGlobalSection, "wbenabled"}},
      cfg_.wb_enabled);
  cfg_.wb_ack_latency_cycles = get_u64_any(
      ini,
      {{"hil", "wbacklatencycycles"},
       {kGlobalSection, "wb_ack_latency_cycles"},
       {kGlobalSection, "wbacklatencycycles"}},
      cfg_.wb_ack_latency_cycles);
  const bool has_icl_capacity =
      has_any(ini,
              {{"hil", "iclcapacitybytes"},
               {kGlobalSection, "icl_capacity_bytes"},
               {kGlobalSection, "iclcapacitybytes"}});
  cfg_.icl_capacity_bytes = get_u64_any(
      ini,
      {{"hil", "iclcapacitybytes"}, {kGlobalSection, "icl_capacity_bytes"},
       {kGlobalSection, "iclcapacitybytes"}},
      cfg_.icl_capacity_bytes);
  cfg_.icl_line_size_bytes = static_cast<uint32_t>(get_u64_any(
      ini,
      {{"hil", "icllinesizebytes"}, {kGlobalSection, "icl_line_size_bytes"},
       {kGlobalSection, "icllinesizebytes"}},
      cfg_.icl_line_size_bytes == 0 ? cfg_.page_bytes
                                    : cfg_.icl_line_size_bytes));
  cfg_.icl_eviction_policy = lower(trim(get_string_any(
      ini,
      {{"hil", "iclevictionpolicy"},
       {kGlobalSection, "icl_eviction_policy"},
       {kGlobalSection, "iclevictionpolicy"}},
      cfg_.icl_eviction_policy)));
  cfg_.icl_associativity = lower(trim(get_string_any(
      ini,
      {{"hil", "iclassociativity"}, {kGlobalSection, "icl_associativity"},
       {kGlobalSection, "iclassociativity"}},
      cfg_.icl_associativity)));

  if (cfg_.wb_enabled && !has_icl_capacity && cfg_.icl_capacity_bytes == 0) {
    cfg_.icl_capacity_bytes = cfg_.page_bytes;
  }
  if (cfg_.icl_eviction_policy != "lru") {
    die_config("icl_eviction_policy must be \"lru\"");
  }
  if (cfg_.icl_associativity != "full") {
    die_config("icl_associativity must be \"full\"");
  }
  if (cfg_.icl_line_size_bytes != cfg_.page_bytes) {
    die_config("icl_line_size_bytes must equal page_size_bytes");
  }
  if (cfg_.wb_enabled) {
    if (cfg_.icl_capacity_bytes == 0) {
      die_config("icl_capacity_bytes must be nonzero when wb_enabled is true");
    }
    if (cfg_.icl_capacity_bytes % cfg_.icl_line_size_bytes != 0) {
      die_config("icl_capacity_bytes must be a multiple of icl_line_size_bytes");
    }
  }

  configure_resources();
}

void SsdLatencyModel::reset() { configure_resources(); }

void SsdLatencyModel::tick(uint64_t t_target_cycle) {
  while (!pending_nand_completions_.empty() &&
         pending_nand_completions_.top() <= t_target_cycle) {
    pending_nand_completions_.pop();
  }
}

uint64_t SsdLatencyModel::submit(const SsdRequest &req, uint64_t t_arrival) {
  return submit_timing(req, t_arrival).host_complete_cycle;
}

SsdSubmitResult SsdLatencyModel::submit_timing(const SsdRequest &req,
                                               uint64_t t_arrival) {
  SsdSubmitResult result;
  if (req.len == 0) {
    result.host_complete_cycle =
        t_arrival + cfg_.completion_overhead_cycles + cfg_.host_overhead_cycles;
    result.nand_complete_cycle = t_arrival;
    result.icl_occupancy_lines = icl_occupancy_lines();
    result.icl_dirty_lines = icl_dirty_lines();
    result.icl_evictions_total = icl_evictions_total();
    result.icl_writeback_bytes_total = icl_writeback_bytes_total();
    return result;
  }

  const uint64_t start_byte = static_cast<uint64_t>(req.offset) * 512ULL;
  const uint64_t bytes = static_cast<uint64_t>(req.len) * 512ULL;
  const uint64_t end_byte = start_byte + bytes;
  const uint64_t first_lpn = start_byte / cfg_.page_bytes;
  const uint64_t last_lpn = (end_byte - 1) / cfg_.page_bytes;

  if (req.write && cfg_.wb_enabled) {
    return submit_write_buffered(t_arrival,
                                 start_byte,
                                 bytes,
                                 first_lpn,
                                 last_lpn);
  }

  if (!req.write && cfg_.wb_enabled &&
      icl_read_all_hit(first_lpn, last_lpn)) {
    return submit_read_cache_hit(t_arrival, first_lpn, last_lpn);
  }

  uint64_t t_done = t_arrival + cfg_.cmd_overhead_cycles;
  for (uint64_t lpn = first_lpn; lpn <= last_lpn; lpn++) {
    const uint64_t page_start = lpn * cfg_.page_bytes;
    const uint64_t page_end = page_start + cfg_.page_bytes;
    const uint64_t op_start = std::max(start_byte, page_start);
    const uint64_t op_end = std::min(end_byte, page_end);
    t_done = std::max(t_done,
                      schedule_page_op(map_lpn(lpn),
                                       lpn,
                                       req.write,
                                       op_end - op_start,
                                       t_arrival + cfg_.cmd_overhead_cycles));
  }

  pending_nand_completions_.push(t_done);
  result.nand_complete_cycle = t_done;

  const uint64_t host_transfer =
      transfer_cycles(bytes, cfg_.host_bytes_per_sec);
  const uint64_t host_start =
      reserve_host(t_done + cfg_.completion_overhead_cycles, host_transfer);
  result.host_complete_cycle =
      host_start + host_transfer + cfg_.host_overhead_cycles;
  result.write_ack_before_nand = false;
  result.icl_occupancy_lines = icl_occupancy_lines();
  result.icl_dirty_lines = icl_dirty_lines();
  result.icl_evictions_total = icl_evictions_total();
  result.icl_writeback_bytes_total = icl_writeback_bytes_total();
  return result;
}

uint64_t SsdLatencyModel::peek(const SsdRequest &req,
                               uint64_t t_arrival) const {
  SsdLatencyModel copy = *this;
  return copy.submit(req, t_arrival);
}

void SsdLatencyModel::configure_resources() {
  channel_reservations_.assign(cfg_.channels, {});
  die_busy_until_.assign(
      static_cast<size_t>(cfg_.channels) * cfg_.ways * cfg_.dies *
          cfg_.planes,
      0);
  pending_nand_completions_ =
      std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>>();
  host_reservations_.clear();
  write_buffer_ = WriteBuffer();
  if (cfg_.wb_enabled && cfg_.icl_capacity_bytes != 0) {
    write_buffer_.capacity_lines =
        cfg_.icl_capacity_bytes / cfg_.icl_line_size_bytes;
  }
}

uint64_t SsdLatencyModel::icl_occupancy_lines() const {
  return write_buffer_.lines.size();
}

uint64_t SsdLatencyModel::icl_dirty_lines() const {
  return write_buffer_.dirty_lines;
}

uint64_t SsdLatencyModel::icl_evictions_total() const {
  return write_buffer_.evictions_total;
}

uint64_t SsdLatencyModel::icl_writeback_bytes_total() const {
  return write_buffer_.writeback_bytes_total;
}

SsdLatencyModel::Address SsdLatencyModel::map_lpn(uint64_t lpn) const {
  Address addr;
  addr.channel = static_cast<uint32_t>(lpn % cfg_.channels);
  lpn /= cfg_.channels;
  addr.way = static_cast<uint32_t>(lpn % cfg_.ways);
  lpn /= cfg_.ways;
  addr.die = static_cast<uint32_t>(lpn % cfg_.dies);
  lpn /= cfg_.dies;
  addr.plane = static_cast<uint32_t>(lpn % cfg_.planes);
  return addr;
}

void SsdLatencyModel::touch_write_buffer_line(uint64_t lpn,
                                              uint64_t t_access) {
  std::unordered_map<uint64_t, WriteBufferLine>::iterator it =
      write_buffer_.lines.find(lpn);
  assert(it != write_buffer_.lines.end());
  it->second.last_access_cycle = t_access;
  write_buffer_.lru.remove(lpn);
  write_buffer_.lru.push_front(lpn);
}

void SsdLatencyModel::insert_write_buffer_line(uint64_t lpn,
                                               uint64_t t_access) {
  WriteBufferLine line;
  line.lpn = lpn;
  line.dirty = true;
  line.insert_cycle = t_access;
  line.last_access_cycle = t_access;
  write_buffer_.lines.emplace(lpn, line);
  write_buffer_.lru.push_front(lpn);
  write_buffer_.dirty_lines++;
}

uint64_t SsdLatencyModel::evict_write_buffer_line(uint64_t t_arrival,
                                                  uint64_t *victim_lpn) {
  assert(!write_buffer_.lru.empty());
  const uint64_t evicted_lpn = write_buffer_.lru.back();
  write_buffer_.lru.pop_back();

  std::unordered_map<uint64_t, WriteBufferLine>::iterator victim =
      write_buffer_.lines.find(evicted_lpn);
  assert(victim != write_buffer_.lines.end());

  uint64_t writeback_done = t_arrival;
  if (victim->second.dirty) {
    write_buffer_.dirty_lines--;
    write_buffer_.writeback_bytes_total += cfg_.icl_line_size_bytes;
    writeback_done = schedule_page_op(map_lpn(evicted_lpn),
                                      evicted_lpn,
                                      true,
                                      cfg_.icl_line_size_bytes,
                                      t_arrival);
    pending_nand_completions_.push(writeback_done);
  }

  write_buffer_.lines.erase(victim);
  write_buffer_.evictions_total++;
  if (victim_lpn != nullptr) {
    *victim_lpn = evicted_lpn;
  }
  return writeback_done;
}

SsdSubmitResult SsdLatencyModel::submit_write_buffered(uint64_t t_arrival,
                                                       uint64_t start_byte,
                                                       uint64_t bytes,
                                                       uint64_t first_lpn,
                                                       uint64_t last_lpn) {
  SsdSubmitResult result;
  uint64_t writeback_done = t_arrival;
  uint64_t fetch_done = t_arrival;
  bool all_hit = true;
  const bool full_page_write =
      bytes == cfg_.page_bytes && (start_byte % cfg_.page_bytes) == 0;

  for (uint64_t lpn = first_lpn; lpn <= last_lpn; lpn++) {
    const bool hit = write_buffer_.lines.find(lpn) != write_buffer_.lines.end();
    all_hit = all_hit && hit;
    if (hit) {
      WriteBufferLine &line = write_buffer_.lines[lpn];
      if (!line.dirty) {
        line.dirty = true;
        write_buffer_.dirty_lines++;
      }
      touch_write_buffer_line(lpn, t_arrival);
      continue;
    }

    uint64_t victim_done = t_arrival;
    if (write_buffer_.lines.size() >= write_buffer_.capacity_lines) {
      uint64_t victim_lpn = SsdSubmitResult::kNoVictim;
      victim_done = evict_write_buffer_line(t_arrival, &victim_lpn);
      writeback_done = std::max(writeback_done, victim_done);
      result.icl_eviction_triggered = true;
      result.icl_victim_lpn = victim_lpn;
      result.writeback_scheduled = result.writeback_scheduled ||
                                   victim_done > t_arrival;
    }

    if (!full_page_write) {
      const uint64_t this_fetch_done = schedule_page_op(map_lpn(lpn),
                                                        lpn,
                                                        false,
                                                        cfg_.icl_line_size_bytes,
                                                        t_arrival);
      pending_nand_completions_.push(this_fetch_done);
      fetch_done = std::max(fetch_done, this_fetch_done);
      result.icl_rmw_triggered = true;
    }

    insert_write_buffer_line(lpn, t_arrival);
  }

  const uint64_t base_ack =
      t_arrival + cfg_.wb_ack_latency_cycles + cfg_.host_overhead_cycles;
  if (result.icl_rmw_triggered) {
    result.host_complete_cycle =
        std::max(std::max(writeback_done, fetch_done), t_arrival) +
        cfg_.wb_ack_latency_cycles + cfg_.host_overhead_cycles;
  } else {
    result.host_complete_cycle = std::max(base_ack, writeback_done);
  }
  result.nand_complete_cycle = std::max(writeback_done, fetch_done);
  result.write_ack_before_nand =
      result.writeback_scheduled &&
      result.host_complete_cycle < result.nand_complete_cycle;

  result.icl_was_hit = all_hit;
  result.icl_occupancy_lines = icl_occupancy_lines();
  result.icl_dirty_lines = icl_dirty_lines();
  result.icl_evictions_total = icl_evictions_total();
  result.icl_writeback_bytes_total = icl_writeback_bytes_total();
  result.icl_writeback_latency_cycles =
      writeback_done > t_arrival ? writeback_done - t_arrival : 0;
  result.icl_rmw_fetch_cycles =
      fetch_done > t_arrival ? fetch_done - t_arrival : 0;
  return result;
}

bool SsdLatencyModel::icl_read_all_hit(uint64_t first_lpn,
                                       uint64_t last_lpn) const {
  for (uint64_t lpn = first_lpn; lpn <= last_lpn; lpn++) {
    if (write_buffer_.lines.find(lpn) == write_buffer_.lines.end()) {
      return false;
    }
  }
  return true;
}

SsdSubmitResult SsdLatencyModel::submit_read_cache_hit(uint64_t t_arrival,
                                                      uint64_t first_lpn,
                                                      uint64_t last_lpn) {
  SsdSubmitResult result;
  for (uint64_t lpn = first_lpn; lpn <= last_lpn; lpn++) {
    touch_write_buffer_line(lpn, t_arrival);
  }
  result.icl_was_read_hit = true;
  result.host_complete_cycle = t_arrival + cfg_.host_overhead_cycles;
  result.nand_complete_cycle = t_arrival;
  result.icl_occupancy_lines = icl_occupancy_lines();
  result.icl_dirty_lines = icl_dirty_lines();
  result.icl_evictions_total = icl_evictions_total();
  result.icl_writeback_bytes_total = icl_writeback_bytes_total();
  return result;
}

uint64_t SsdLatencyModel::reserve_channel(uint32_t channel,
                                          uint64_t earliest,
                                          uint64_t cycles) {
  if (cycles == 0) {
    return earliest;
  }

  std::vector<std::pair<uint64_t, uint64_t>> &reservations =
      channel_reservations_[channel];
  uint64_t start = earliest;
  std::vector<std::pair<uint64_t, uint64_t>>::iterator insert_at =
      reservations.begin();

  for (; insert_at != reservations.end(); ++insert_at) {
    if (start + cycles <= insert_at->first) {
      break;
    }
    if (start < insert_at->second) {
      start = insert_at->second;
    }
  }

  reservations.insert(insert_at, std::make_pair(start, start + cycles));
  return start;
}

uint64_t SsdLatencyModel::reserve_host(uint64_t earliest, uint64_t cycles) {
  if (cycles == 0) {
    return earliest;
  }
  uint64_t start = earliest;
  std::vector<std::pair<uint64_t, uint64_t>>::iterator insert_at =
      host_reservations_.begin();

  for (; insert_at != host_reservations_.end(); ++insert_at) {
    if (start + cycles <= insert_at->first) {
      break;
    }
    if (start < insert_at->second) {
      start = insert_at->second;
    }
  }

  host_reservations_.insert(insert_at, std::make_pair(start, start + cycles));
  return start;
}

uint64_t SsdLatencyModel::schedule_page_op(const Address &addr,
                                           uint64_t ppn,
                                           bool write,
                                           uint64_t bytes,
                                           uint64_t t_arrival) {
  uint64_t &die_busy = die_busy_until_[die_index(addr)];
  const PageType type = page_type(ppn);

  const uint64_t pre_dma =
      write ? transfer_cycles(bytes, cfg_.dma_bytes_per_sec) : 1;
  const uint64_t mem_op = write ? program_cycles(type) : read_cycles(type);
  const uint64_t post_dma =
      write ? 1 : transfer_cycles(bytes, cfg_.dma_bytes_per_sec);

  const uint64_t t_pre_start =
      reserve_channel(addr.channel, std::max(t_arrival, die_busy), pre_dma);
  const uint64_t t_pre_end = t_pre_start + pre_dma;
  const uint64_t t_mem_start = t_pre_end;
  const uint64_t t_mem_end = t_mem_start + mem_op;
  const uint64_t t_post_start =
      reserve_channel(addr.channel, t_mem_end, post_dma);
  const uint64_t t_post_end = t_post_start + post_dma;

  die_busy = t_mem_end;
  return t_post_end;
}

PageType SsdLatencyModel::page_type(uint64_t ppn) const {
  if (cfg_.cell_type == SsdCellType::SLC) {
    return PageType::SINGLE;
  }

  const uint64_t addr_in_block = ppn % cfg_.pages_per_block;
  if (addr_in_block < cfg_.n_meta_pages) {
    return PageType::LSB;
  }

  const uint64_t f =
      ((addr_in_block - cfg_.n_meta_pages) / cfg_.planes) %
      cfg_.bits_per_cell;
  if (f == 0) {
    return PageType::LSB;
  }
  if (f == 1) {
    return PageType::CSB;
  }
  return PageType::MSB;
}

uint64_t SsdLatencyModel::read_cycles(PageType type) const {
  switch (type) {
    case PageType::SINGLE:
    case PageType::LSB:
      return cfg_.read_lsb_cycles;
    case PageType::CSB:
      return cfg_.read_csb_cycles;
    case PageType::MSB:
      return cfg_.read_msb_cycles;
  }
  return cfg_.read_lsb_cycles;
}

uint64_t SsdLatencyModel::program_cycles(PageType type) const {
  switch (type) {
    case PageType::SINGLE:
    case PageType::LSB:
      return cfg_.program_lsb_cycles;
    case PageType::CSB:
      return cfg_.program_csb_cycles;
    case PageType::MSB:
      return cfg_.program_msb_cycles;
  }
  return cfg_.program_lsb_cycles;
}

uint64_t SsdLatencyModel::transfer_cycles(uint64_t bytes,
                                          double bytes_per_sec) const {
  if (bytes == 0) {
    return 0;
  }
  const long double cycles =
      (static_cast<long double>(bytes) * cfg_.target_clock_hz) /
      static_cast<long double>(bytes_per_sec);
  const long double ceiled = std::ceil(cycles);
  if (ceiled > static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
    die_config("transfer latency overflow");
  }
  const uint64_t result = static_cast<uint64_t>(ceiled);
  return result == 0 ? 1 : result;
}

uint64_t SsdLatencyModel::ns_to_cycles(uint64_t ns) const {
  const long double cycles =
      (static_cast<long double>(ns) * cfg_.target_clock_hz) / 1000000000.0L;
  const long double ceiled = std::ceil(cycles);
  if (ceiled > static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
    die_config("nanosecond latency overflow");
  }
  return static_cast<uint64_t>(ceiled);
}

uint64_t SsdLatencyModel::die_index(const Address &addr) const {
  return ((static_cast<uint64_t>(addr.channel) * cfg_.ways + addr.way) *
              cfg_.dies +
          addr.die) *
             cfg_.planes +
         addr.plane;
}
