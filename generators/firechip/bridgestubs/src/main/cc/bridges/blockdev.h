// See LICENSE for license details
#ifndef __BLOCKDEV_H
#define __BLOCKDEV_H

#include <queue>
#include <stdio.h>
#include <string>
#include <vector>

#include "bridges/ssd_latency_model.h"
#include "core/bridge_driver.h"

struct BLOCKDEVBRIDGEMODULE_struct {
  uint64_t read_latency;
  uint64_t write_latency;
  uint64_t bdev_nsectors;
  uint64_t bdev_max_req_len;
  uint64_t bdev_req_valid;
  uint64_t bdev_req_write;
  uint64_t bdev_req_offset;
  uint64_t bdev_req_len;
  uint64_t bdev_req_tag;
  uint64_t bdev_req_ready;
  uint64_t bdev_data_valid;
  // 256-bit beat exposed as eight 32-bit registers.
  // _0 = bits[31:0], _1 = bits[63:32], ..., _7 = bits[255:224].
  uint64_t bdev_data_data_0;
  uint64_t bdev_data_data_1;
  uint64_t bdev_data_data_2;
  uint64_t bdev_data_data_3;
  uint64_t bdev_data_data_4;
  uint64_t bdev_data_data_5;
  uint64_t bdev_data_data_6;
  uint64_t bdev_data_data_7;
  uint64_t bdev_data_tag;
  uint64_t bdev_data_ready;
  uint64_t bdev_rresp_data_0;
  uint64_t bdev_rresp_data_1;
  uint64_t bdev_rresp_data_2;
  uint64_t bdev_rresp_data_3;
  uint64_t bdev_rresp_data_4;
  uint64_t bdev_rresp_data_5;
  uint64_t bdev_rresp_data_6;
  uint64_t bdev_rresp_data_7;
  uint64_t bdev_rresp_tag;
  uint64_t bdev_rresp_valid;
  uint64_t bdev_rresp_ready;
  uint64_t bdev_wack_tag;
  uint64_t bdev_wack_valid;
  uint64_t bdev_wack_ready;
  uint64_t bdev_reqs_pending;
  uint64_t bdev_wack_stalled;
  uint64_t bdev_rresp_stalled;
  uint64_t bdev_target_cycle;
  uint64_t bdev_host_timing;
};

#define SECTOR_SIZE 512
#define SECTOR_SHIFT 9
#define SECTOR_BEATS (SECTOR_SIZE / 32)
#define BEAT_WORDS 4
#define MAX_REQ_LEN 32

struct blkdev_request {
  bool write;
  uint32_t offset;
  uint32_t len;
  uint32_t tag;
};

struct blkdev_data {
  uint64_t data[BEAT_WORDS];
  uint32_t tag;
};

struct blkdev_write_tracker {
  uint64_t offset;
  uint64_t count;
  uint64_t size;
  uint32_t req_offset;
  uint32_t req_len;
  uint64_t data[MAX_REQ_LEN * SECTOR_BEATS * BEAT_WORDS];
};

struct blkdev_completion {
  uint64_t t_complete;
  uint64_t seq;
  bool write;
  uint32_t tag;
  std::vector<blkdev_data> read_data;
  bool ack_already_sent;
};

struct blkdev_completion_compare {
  bool operator()(const blkdev_completion &lhs,
                  const blkdev_completion &rhs) const {
    if (lhs.t_complete != rhs.t_complete) {
      return lhs.t_complete > rhs.t_complete;
    }
    return lhs.seq > rhs.seq;
  }
};

class blockdev_t : public bridge_driver_t {
public:
  /// The identifier for the bridge type used for casts.
  static char KIND;

  blockdev_t(simif_t &sim,
             const BLOCKDEVBRIDGEMODULE_struct &mmio_addrs,
             int blkdevno,
             const std::vector<std::string> &args,
             uint32_t num_trackers,
             uint32_t latency_bits);
  ~blockdev_t() override;

  uint32_t nsectors(void) { return _nsectors; }
  uint32_t max_request_length(void) { return MAX_REQ_LEN; }

  void init() override;
  void tick() override;

  void send();
  void recv();

private:
#ifdef BLOCKDEV_UNIT_TEST
  friend struct blockdev_test_access;
#endif

  const BLOCKDEVBRIDGEMODULE_struct mmio_addrs;

  // Set if, on the previous tick, we couldn't write back all of our response
  // data
  bool resp_data_pending = false;

  uint32_t _ntags;
  uint32_t _nsectors;
  FILE *_file, *logfile;
  char *filename = nullptr;
  std::queue<blkdev_request> requests;
  std::queue<blkdev_data> req_data;
  std::queue<blkdev_data> read_responses;
  std::queue<uint32_t> write_acks;
  std::priority_queue<blkdev_completion,
                      std::vector<blkdev_completion>,
                      blkdev_completion_compare>
      completions;

  std::vector<blkdev_write_tracker> write_trackers;

  void do_read(struct blkdev_request &req);
  void do_write(struct blkdev_request &req);
  bool can_accept(struct blkdev_data &data);
  void handle_data(struct blkdev_data &data, uint64_t t_now = 0);
  std::vector<blkdev_data> read_request_data(struct blkdev_request &req);
  void schedule_read(struct blkdev_request &req, uint64_t t_now);
  void schedule_write_ack(uint32_t tag,
                          uint32_t offset,
                          uint32_t len,
                          uint64_t t_now);
  void push_completion(const blkdev_completion &entry);
  void release_ready_completions(uint64_t t_now);
  void tick_fixed();
  void tick_ssd();
  // Returns true if no widget interaction is required
  bool idle();

  // Default timing model parameters
  uint32_t read_latency = 4096;
  uint32_t write_latency = 4096;

  bool ssd_timing_enabled = false;
  std::string ssd_config_path;
  SsdLatencyModel ssd_model;
  uint64_t completion_seq = 0;
  uint64_t write_csv_seq_ = 0;
  uint64_t read_csv_seq_ = 0;
};

#endif // __BLOCKDEV_H
