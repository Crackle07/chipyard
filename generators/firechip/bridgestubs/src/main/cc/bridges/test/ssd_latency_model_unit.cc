#include "bridges/ssd_latency_model.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

static std::string write_config(const std::string &body) {
  char path[] = "/tmp/ssd-latency-model-test-XXXXXX";
  const int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  std::ofstream out(path);
  out << body;
  out.close();
  return std::string(path);
}

static std::string base_config(const std::string &extra_pal = "") {
  return std::string(
             "[sim]\n"
             "FixedPolicy=false\n"
             "TargetClockHz=1000000000\n"
             "\n"
             "[pal]\n"
             "Channel=2\n"
             "Way=1\n"
             "Die=1\n"
             "Plane=1\n"
             "PageSize=512\n"
             "DMASpeed=512000000000\n"
             "NANDType=SLC\n"
             "Read.LSB=10\n"
             "Program.LSB=20\n") +
         extra_pal +
         std::string(
             "\n"
             "[hil]\n"
             "CmdOverhead=2\n"
             "CompletionOverhead=3\n"
             "HostBandwidth=512000000000\n");
}

static SsdLatencyModel load_model(const std::string &config) {
  SsdLatencyModel model;
  const std::string path = write_config(config);
  model.load_config(path);
  unlink(path.c_str());
  return model;
}

static void test_single_read_latency() {
  SsdLatencyModel model = load_model(base_config());
  SsdRequest req;
  req.offset = 0;
  req.len = 1;
  const uint64_t completion = model.submit(req, 0);
  assert(completion == 18);
}

static void test_multi_page_split_parallel_channels() {
  SsdLatencyModel model = load_model(base_config());
  SsdRequest req;
  req.offset = 0;
  req.len = 2;
  const uint64_t completion = model.submit(req, 0);
  assert(completion == 19);
}

static void test_unaligned_request_touches_two_lpns() {
  SsdLatencyModel model =
      load_model(base_config("Channel=1\nPageSize=768\n"));
  SsdRequest req;
  req.offset = 1;
  req.len = 1;
  const uint64_t completion = model.submit(req, 0);
  assert(completion == 30);
}

static void test_same_resource_serializes() {
  SsdLatencyModel model = load_model(base_config());
  SsdRequest first;
  first.offset = 0;
  first.len = 1;
  SsdRequest second;
  second.offset = 2;
  second.len = 1;

  const uint64_t first_done = model.submit(first, 0);
  const uint64_t second_done = model.submit(second, 0);
  assert(first_done == 18);
  assert(second_done == 30);
}

static void test_same_channel_different_dies_interleave() {
  SsdLatencyModel model = load_model(base_config("Channel=1\nDie=2\n"));
  SsdRequest first;
  first.offset = 0;
  first.len = 1;
  SsdRequest second;
  second.offset = 1;
  second.len = 1;

  const uint64_t first_done = model.submit(first, 0);
  const uint64_t second_done = model.submit(second, 0);
  assert(first_done == 18);
  assert(second_done == 19);
}

static void test_different_channels_parallel() {
  SsdLatencyModel model = load_model(base_config());
  SsdRequest first;
  first.offset = 0;
  first.len = 1;
  SsdRequest second;
  second.offset = 1;
  second.len = 1;

  const uint64_t first_done = model.submit(first, 0);
  const uint64_t second_done = model.submit(second, 0);
  assert(first_done == 18);
  assert(second_done == 19);
}

static void test_host_bandwidth_serializes_completions() {
  SsdLatencyModel model = load_model(
      base_config("\n")
      + "HostBandwidth=512000000\n");
  SsdRequest first;
  first.offset = 0;
  first.len = 1;
  SsdRequest second;
  second.offset = 1;
  second.len = 1;

  const uint64_t first_done = model.submit(first, 0);
  const uint64_t second_done = model.submit(second, 0);
  assert(first_done == 1017);
  assert(second_done == 2017);
}

static void test_peek_does_not_commit() {
  SsdLatencyModel model = load_model(base_config());
  SsdRequest req;
  req.offset = 0;
  req.len = 1;

  const uint64_t peek_done = model.peek(req, 0);
  const uint64_t submit_done = model.submit(req, 0);
  assert(peek_done == submit_done);
}

static void test_tlc_page_type_lookup() {
  SsdLatencyModel model = load_model(
      "[sim]\n"
      "FixedPolicy=false\n"
      "TargetClockHz=1000000000\n"
      "\n"
      "[pal]\n"
      "Channel=1\n"
      "Way=1\n"
      "Die=1\n"
      "Plane=2\n"
      "PagesPerBlock=1024\n"
      "PageSize=16384\n"
      "NANDType=TLC\n"
      "BitsPerCell=3\n"
      "NMetaPages=8\n"
      "\n"
      "[hil]\n");

  for (uint64_t ppn = 0; ppn < 10; ppn++) {
    assert(model.page_type(ppn) == PageType::LSB);
  }
  assert(model.page_type(10) == PageType::CSB);
  assert(model.page_type(11) == PageType::CSB);
  assert(model.page_type(12) == PageType::MSB);
  assert(model.page_type(13) == PageType::MSB);
}

static void test_write_buffer_fill_and_hit_no_pal() {
  SsdLatencyModel model = load_model(
      base_config()
      + "WBEnabled=true\n"
        "WBAckLatencyCycles=5\n"
        "HostOverheadCycles=7\n"
        "ICLCapacityBytes=1024\n"
        "ICLLineSizeBytes=512\n");

  SsdRequest write;
  write.write = true;
  write.offset = 0;
  write.len = 1;

  const SsdSubmitResult miss = model.submit_timing(write, 10);
  assert(miss.host_complete_cycle == 22);
  assert(miss.nand_complete_cycle == 10);
  assert(!miss.icl_was_hit);
  assert(!miss.icl_eviction_triggered);
  assert(model.pending_count() == 0);
  assert(model.icl_occupancy_lines() == 1);
  assert(model.icl_dirty_lines() == 1);

  const SsdSubmitResult hit = model.submit_timing(write, 20);
  assert(hit.host_complete_cycle == 32);
  assert(hit.nand_complete_cycle == 20);
  assert(hit.icl_was_hit);
  assert(!hit.icl_eviction_triggered);
  assert(model.pending_count() == 0);
  assert(model.icl_occupancy_lines() == 1);
  assert(model.icl_dirty_lines() == 1);
}

static void test_write_buffer_eviction_backpressures_ack() {
  SsdLatencyModel model = load_model(
      base_config("Channel=1\nProgram.LSB=100\n")
      + "WBEnabled=true\n"
        "WBAckLatencyCycles=5\n"
        "HostOverheadCycles=7\n"
        "ICLCapacityBytes=512\n"
        "ICLLineSizeBytes=512\n");

  SsdRequest first;
  first.write = true;
  first.offset = 0;
  first.len = 1;
  const SsdSubmitResult first_result = model.submit_timing(first, 0);
  assert(first_result.host_complete_cycle == 12);
  assert(model.pending_count() == 0);

  SsdRequest second;
  second.write = true;
  second.offset = 1;
  second.len = 1;
  const SsdSubmitResult eviction = model.submit_timing(second, 0);
  assert(eviction.icl_eviction_triggered);
  assert(eviction.writeback_scheduled);
  assert(eviction.icl_writeback_latency_cycles == 102);
  assert(eviction.host_complete_cycle == 102);
  assert(eviction.nand_complete_cycle == 102);
  assert(model.pending_count() == 1);
  assert(model.icl_evictions_total() == 1);
  assert(model.icl_writeback_bytes_total() == 512);

  model.tick(101);
  assert(model.pending_count() == 1);
  model.tick(102);
  assert(model.pending_count() == 0);
}

static uint64_t tlc_writeback_latency_for_lpn(uint32_t lpn) {
  SsdLatencyModel model = load_model(
      "[sim]\n"
      "FixedPolicy=false\n"
      "TargetClockHz=1000000000\n"
      "\n"
      "[pal]\n"
      "Channel=1\n"
      "Way=1\n"
      "Die=1\n"
      "Plane=2\n"
      "PagesPerBlock=1024\n"
      "PageSize=512\n"
      "DMASpeed=512000000000\n"
      "NANDType=TLC\n"
      "BitsPerCell=3\n"
      "NMetaPages=8\n"
      "Read.LSB=10\n"
      "Read.CSB=20\n"
      "Read.MSB=30\n"
      "Program.LSB=10\n"
      "Program.CSB=20\n"
      "Program.MSB=30\n"
      "\n"
      "[hil]\n"
      "WBEnabled=true\n"
      "WBAckLatencyCycles=0\n"
      "HostOverheadCycles=0\n"
      "ICLCapacityBytes=512\n"
      "ICLLineSizeBytes=512\n");

  SsdRequest fill;
  fill.write = true;
  fill.offset = lpn;
  fill.len = 1;
  (void) model.submit_timing(fill, 0);

  SsdRequest evict;
  evict.write = true;
  evict.offset = 100;
  evict.len = 1;
  const SsdSubmitResult result = model.submit_timing(evict, 0);
  assert(result.icl_eviction_triggered);
  return result.icl_writeback_latency_cycles;
}

static void test_tlc_writeback_uses_page_type_program_latency() {
  assert(tlc_writeback_latency_for_lpn(8) == 12);
  assert(tlc_writeback_latency_for_lpn(10) == 22);
  assert(tlc_writeback_latency_for_lpn(12) == 32);
}

static void test_write_buffer_lru_eviction_order() {
  SsdLatencyModel model = load_model(
      "[sim]\n"
      "FixedPolicy=false\n"
      "TargetClockHz=1000000000\n"
      "\n"
      "[pal]\n"
      "Channel=1\n"
      "Way=1\n"
      "Die=1\n"
      "Plane=2\n"
      "PagesPerBlock=1024\n"
      "PageSize=512\n"
      "DMASpeed=512000000000\n"
      "NANDType=TLC\n"
      "BitsPerCell=3\n"
      "NMetaPages=8\n"
      "Read.LSB=10\n"
      "Read.CSB=20\n"
      "Read.MSB=30\n"
      "Program.LSB=10\n"
      "Program.CSB=20\n"
      "Program.MSB=30\n"
      "\n"
      "[hil]\n"
      "WBEnabled=true\n"
      "WBAckLatencyCycles=0\n"
      "HostOverheadCycles=0\n"
      "ICLCapacityBytes=1024\n"
      "ICLLineSizeBytes=512\n");

  SsdRequest req;
  req.write = true;
  req.len = 1;

  req.offset = 8;
  (void) model.submit_timing(req, 0);
  req.offset = 10;
  (void) model.submit_timing(req, 0);
  req.offset = 8;
  const SsdSubmitResult hit = model.submit_timing(req, 0);
  assert(hit.icl_was_hit);

  req.offset = 12;
  const SsdSubmitResult eviction = model.submit_timing(req, 0);
  assert(eviction.icl_eviction_triggered);
  // LPN 8 was touched most recently, so LRU evicts LPN 10 (CSB: 20 + 2 DMA).
  assert(eviction.icl_writeback_latency_cycles == 22);
  assert(eviction.icl_victim_lpn == 10);
}

static void test_write_buffer_reports_no_victim_when_no_eviction() {
  SsdLatencyModel model = load_model(
      "[sim]\n"
      "FixedPolicy=false\n"
      "TargetClockHz=1000000000\n"
      "\n"
      "[pal]\n"
      "Channel=1\n"
      "Way=1\n"
      "Die=1\n"
      "Plane=1\n"
      "PageSize=512\n"
      "DMASpeed=512000000000\n"
      "NANDType=SLC\n"
      "Read.LSB=10\n"
      "Program.LSB=20\n"
      "\n"
      "[hil]\n"
      "WBEnabled=true\n"
      "WBAckLatencyCycles=0\n"
      "HostOverheadCycles=0\n"
      "ICLCapacityBytes=1024\n"
      "ICLLineSizeBytes=512\n");

  SsdRequest req;
  req.write = true;
  req.len = 1;
  req.offset = 0;
  const SsdSubmitResult cold = model.submit_timing(req, 0);
  assert(!cold.icl_was_hit);
  assert(!cold.icl_eviction_triggered);
  assert(cold.icl_victim_lpn == SsdSubmitResult::kNoVictim);

  req.offset = 0;
  const SsdSubmitResult hit = model.submit_timing(req, 0);
  assert(hit.icl_was_hit);
  assert(!hit.icl_eviction_triggered);
  assert(hit.icl_victim_lpn == SsdSubmitResult::kNoVictim);
}

static SsdLatencyModel load_rmw_model(uint64_t capacity_bytes = 2048) {
  return load_model(
      "[sim]\n"
      "FixedPolicy=false\n"
      "TargetClockHz=1000000000\n"
      "\n"
      "[pal]\n"
      "Channel=1\n"
      "Way=1\n"
      "Die=1\n"
      "Plane=1\n"
      "PageSize=1024\n"
      "DMASpeed=512000000000\n"
      "NANDType=SLC\n"
      "Read.LSB=100\n"
      "Program.LSB=200\n"
      "\n"
      "[hil]\n"
      "WBEnabled=true\n"
      "WBAckLatencyCycles=5\n"
      "HostOverheadCycles=7\n"
      "ICLCapacityBytes=" +
      std::to_string(capacity_bytes) +
      "\n"
      "ICLLineSizeBytes=1024\n");
}

static void test_subpage_write_hit_no_rmw() {
  SsdLatencyModel model = load_rmw_model();

  SsdRequest full;
  full.write = true;
  full.offset = 0;
  full.len = 2;  // 1024 B page / 512 B sector.
  const SsdSubmitResult fill = model.submit_timing(full, 0);
  assert(!fill.icl_rmw_triggered);
  assert(fill.host_complete_cycle == 12);

  SsdRequest partial;
  partial.write = true;
  partial.offset = 0;
  partial.len = 1;
  const SsdSubmitResult hit = model.submit_timing(partial, 20);
  assert(hit.icl_was_hit);
  assert(!hit.icl_rmw_triggered);
  assert(hit.icl_rmw_fetch_cycles == 0);
  assert(hit.host_complete_cycle == 32);
  assert(hit.nand_complete_cycle == 20);
  assert(model.pending_count() == 0);
}

static void test_subpage_write_miss_triggers_rmw() {
  SsdLatencyModel model = load_rmw_model();

  SsdRequest partial;
  partial.write = true;
  partial.offset = 0;
  partial.len = 1;
  const SsdSubmitResult miss = model.submit_timing(partial, 0);
  assert(!miss.icl_was_hit);
  assert(miss.icl_rmw_triggered);
  // PAL read of one 1024 B page: pre-DMA 1 + Read.LSB 100 + post-DMA 2.
  assert(miss.icl_rmw_fetch_cycles == 103);
  assert(miss.host_complete_cycle == 115);
  assert(miss.nand_complete_cycle == 103);
  assert(model.pending_count() == 1);
  model.tick(103);
  assert(model.pending_count() == 0);
}

static void test_subpage_write_miss_with_eviction() {
  SsdLatencyModel model = load_rmw_model(1024);

  SsdRequest full;
  full.write = true;
  full.offset = 0;
  full.len = 2;
  const SsdSubmitResult fill = model.submit_timing(full, 0);
  assert(!fill.icl_rmw_triggered);
  assert(model.pending_count() == 0);

  SsdRequest partial;
  partial.write = true;
  partial.offset = 2;  // LPN 1.
  partial.len = 1;
  const SsdSubmitResult rmw = model.submit_timing(partial, 0);
  assert(!rmw.icl_was_hit);
  assert(rmw.icl_rmw_triggered);
  assert(rmw.icl_eviction_triggered);
  assert(rmw.icl_victim_lpn == 0);
  assert(rmw.icl_writeback_latency_cycles == 203);
  // Eviction writeback and fetch are both submitted at cycle 0. The channel
  // and die scheduler serialize their shared resource use deterministically.
  assert(rmw.icl_rmw_fetch_cycles == 306);
  assert(rmw.nand_complete_cycle == 306);
  assert(rmw.host_complete_cycle == 318);
  assert(model.pending_count() == 2);
  model.tick(203);
  assert(model.pending_count() == 1);
  model.tick(306);
  assert(model.pending_count() == 0);
}

static void test_fullpage_write_does_not_trigger_rmw() {
  SsdLatencyModel model = load_rmw_model();

  SsdRequest full;
  full.write = true;
  full.offset = 0;
  full.len = 2;
  const SsdSubmitResult result = model.submit_timing(full, 0);
  assert(!result.icl_was_hit);
  assert(!result.icl_rmw_triggered);
  assert(result.icl_rmw_fetch_cycles == 0);
  assert(result.host_complete_cycle == 12);
  assert(result.nand_complete_cycle == 0);
  assert(model.pending_count() == 0);
}

static SsdLatencyModel load_ryow_model() {
  return load_model(
      "[sim]\n"
      "FixedPolicy=false\n"
      "TargetClockHz=1000000000\n"
      "\n"
      "[pal]\n"
      "Channel=1\n"
      "Way=1\n"
      "Die=1\n"
      "Plane=1\n"
      "PageSize=512\n"
      "DMASpeed=512000000000\n"
      "NANDType=SLC\n"
      "Read.LSB=200\n"
      "Program.LSB=20\n"
      "\n"
      "[hil]\n"
      "WBEnabled=true\n"
      "WBAckLatencyCycles=0\n"
      "HostOverheadCycles=7\n"
      "ICLCapacityBytes=1024\n"
      "ICLLineSizeBytes=512\n");
}

static void test_read_cache_hit_serves_dirty_line() {
  SsdLatencyModel model = load_ryow_model();
  SsdRequest write_req;
  write_req.write = true;
  write_req.offset = 0;
  write_req.len = 1;
  (void) model.submit_timing(write_req, 0);
  assert(model.icl_dirty_lines() == 1);

  SsdRequest read_req;
  read_req.write = false;
  read_req.offset = 0;
  read_req.len = 1;
  const SsdSubmitResult hit = model.submit_timing(read_req, 100);
  assert(hit.icl_was_read_hit);
  // Hit path: t_arrival (100) + host_overhead (7) = 107.
  assert(hit.host_complete_cycle == 107);
  // Dirty bit stays asserted across a read; line is still pending writeback.
  assert(model.icl_dirty_lines() == 1);
}

static void test_read_miss_falls_through_to_pal() {
  SsdLatencyModel model = load_ryow_model();
  SsdRequest read_req;
  read_req.write = false;
  read_req.offset = 0;
  read_req.len = 1;
  const SsdSubmitResult miss = model.submit_timing(read_req, 0);
  assert(!miss.icl_was_read_hit);
  // PAL path latency >> hit-path host_overhead. Spot-check it crossed
  // Read.LSB (200) at minimum.
  assert(miss.host_complete_cycle > 200);
  // No read-allocate: cache stays empty.
  assert(model.icl_occupancy_lines() == 0);
}

static void test_read_hit_updates_lru_position() {
  SsdLatencyModel model = load_ryow_model();
  SsdRequest req;
  req.len = 1;

  req.write = true;
  req.offset = 0;
  (void) model.submit_timing(req, 0);  // LRU: [0]
  req.offset = 1;
  (void) model.submit_timing(req, 0);  // LRU: [1, 0]

  // Read of LPN 0 should refresh it as MRU. Next dirty write evicts LPN 1.
  req.write = false;
  req.offset = 0;
  const SsdSubmitResult hit = model.submit_timing(req, 0);
  assert(hit.icl_was_read_hit);

  req.write = true;
  req.offset = 2;
  const SsdSubmitResult eviction = model.submit_timing(req, 0);
  assert(eviction.icl_eviction_triggered);
  assert(eviction.icl_victim_lpn == 1);
}

static void test_partial_read_hit_treated_as_miss() {
  SsdLatencyModel model = load_ryow_model();
  SsdRequest write_req;
  write_req.write = true;
  write_req.offset = 0;
  write_req.len = 1;
  (void) model.submit_timing(write_req, 0);

  // Cross-page read: offset 0 len 2 sectors at PageSize=512 + SectorSize=512
  // -> two LPNs (0 and 1). LPN 0 cached, LPN 1 not -> miss, no install.
  SsdRequest read_req;
  read_req.write = false;
  read_req.offset = 0;
  read_req.len = 2;
  const SsdSubmitResult miss = model.submit_timing(read_req, 0);
  assert(!miss.icl_was_read_hit);
  assert(model.icl_occupancy_lines() == 1);  // unchanged
}

static void test_bad_config_exits() {
  const std::string path = write_config(base_config("NANDType=wat\n"));
  const pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    SsdLatencyModel model;
    model.load_config(path);
    _exit(0);
  }

  int status = 0;
  waitpid(pid, &status, 0);
  unlink(path.c_str());
  assert(WIFEXITED(status));
  assert(WEXITSTATUS(status) != 0);
}

static void test_bad_icl_config_exits() {
  const std::string path = write_config(
      base_config()
      + "WBEnabled=true\n"
        "ICLCapacityBytes=512\n"
        "ICLLineSizeBytes=512\n"
        "ICLEvictionPolicy=random\n");
  const pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    SsdLatencyModel model;
    model.load_config(path);
    _exit(0);
  }

  int status = 0;
  waitpid(pid, &status, 0);
  unlink(path.c_str());
  assert(WIFEXITED(status));
  assert(WEXITSTATUS(status) != 0);
}

int main() {
  test_single_read_latency();
  test_multi_page_split_parallel_channels();
  test_unaligned_request_touches_two_lpns();
  test_same_resource_serializes();
  test_same_channel_different_dies_interleave();
  test_different_channels_parallel();
  test_host_bandwidth_serializes_completions();
  test_peek_does_not_commit();
  test_tlc_page_type_lookup();
  test_write_buffer_fill_and_hit_no_pal();
  test_write_buffer_eviction_backpressures_ack();
  test_tlc_writeback_uses_page_type_program_latency();
  test_write_buffer_lru_eviction_order();
  test_write_buffer_reports_no_victim_when_no_eviction();
  test_subpage_write_hit_no_rmw();
  test_subpage_write_miss_triggers_rmw();
  test_subpage_write_miss_with_eviction();
  test_fullpage_write_does_not_trigger_rmw();
  test_read_cache_hit_serves_dirty_line();
  test_read_miss_falls_through_to_pal();
  test_read_hit_updates_lru_position();
  test_partial_read_hit_treated_as_miss();
  test_bad_config_exits();
  test_bad_icl_config_exits();
  return 0;
}
