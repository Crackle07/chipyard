#define BLOCKDEV_UNIT_TEST

#include "bridges/blockdev.h"

#include "core/config.h"
#include "core/simif.h"
#include "core/widget.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

widget_t::widget_t(simif_t &simif, const void *kind)
    : simif(simif), kind(kind) {}

widget_t::~widget_t() = default;

simif_t::simif_t(const TargetConfig &config) : config(config) {}

simif_t::~simif_t() = default;

CPUManagedStreamIO &simif_t::get_cpu_managed_stream_io() { abort(); }

FPGAManagedStreamIO &simif_t::get_fpga_managed_stream_io() { abort(); }

std::string_view simif_t::get_target_name() const { return config.target_name; }

int simif_t::run(simulation_t &) { return 0; }

void bridge_driver_t::write(size_t addr, uint32_t data) {
  simif.write(addr, data);
}

uint32_t bridge_driver_t::read(size_t addr) { return simif.read(addr); }

static const TargetConfig kTargetConfig = {
    {0, 0, 32},
    {0, 0, 64},
    0,
    std::nullopt,
    std::nullopt,
    {0, 0},
    "blockdev-host-timing-unit",
};

class FakeSimif final : public simif_t {
public:
  FakeSimif() : simif_t(kTargetConfig) {}

  void write(size_t addr, uint32_t data) override { regs[addr] = data; }

  uint32_t read(size_t addr) override {
    const auto it = regs.find(addr);
    return it == regs.end() ? 0 : it->second;
  }

private:
  std::map<size_t, uint32_t> regs;
};

struct blockdev_test_access {
  static bool idle(blockdev_t &dev) { return dev.idle(); }

  static SsdLatencyModel &model(blockdev_t &dev) { return dev.ssd_model; }

  static void schedule_read(blockdev_t &dev,
                            blkdev_request &req,
                            uint64_t t_now) {
    dev.schedule_read(req, t_now);
  }

  static void do_write(blockdev_t &dev, blkdev_request &req) {
    dev.do_write(req);
  }

  static void handle_data(blockdev_t &dev, blkdev_data &data, uint64_t t_now) {
    dev.handle_data(data, t_now);
  }

  static void release_ready(blockdev_t &dev, uint64_t t_now) {
    dev.ssd_model.tick(t_now);
    dev.release_ready_completions(t_now);
  }

  static uint64_t completion_time_for_tag(blockdev_t &dev, uint32_t tag) {
    auto completions = dev.completions;
    while (!completions.empty()) {
      const blkdev_completion entry = completions.top();
      completions.pop();
      if (entry.tag == tag) {
        return entry.t_complete;
      }
    }
    abort();
  }

  static std::queue<blkdev_data> &read_responses(blockdev_t &dev) {
    return dev.read_responses;
  }

  static std::queue<uint32_t> &write_acks(blockdev_t &dev) {
    return dev.write_acks;
  }

  static uint64_t pending_count(blockdev_t &dev) {
    return dev.ssd_model.pending_count();
  }

  static size_t completion_count(blockdev_t &dev) {
    return dev.completions.size();
  }

  static void flush_file(blockdev_t &dev) { fflush(dev._file); }
};

namespace {

static BLOCKDEVBRIDGEMODULE_struct mmio_addrs() {
  BLOCKDEVBRIDGEMODULE_struct addrs = {};
  addrs.bdev_reqs_pending = 35;
  return addrs;
}

static std::string write_temp_file(const std::string &prefix,
                                   const std::string &body) {
  std::string pattern = "/tmp/" + prefix + "-XXXXXX";
  std::vector<char> path(pattern.begin(), pattern.end());
  path.push_back('\0');
  const int fd = mkstemp(path.data());
  assert(fd >= 0);
  close(fd);

  std::ofstream out(path.data(), std::ios::binary);
  out << body;
  out.close();
  return std::string(path.data());
}

static std::string ssd_config(const std::string &extra_pal = "") {
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
             "Read.LSB=1000\n"
             "Program.LSB=1\n") +
         extra_pal +
         std::string(
             "\n"
             "[hil]\n"
             "CmdOverhead=2\n"
             "CompletionOverhead=3\n"
             "HostBandwidth=512000000000\n");
}

static std::string ssd_config_with_hil(const std::string &extra_pal,
                                       const std::string &extra_hil) {
  return std::string(
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
             "Read.LSB=1000\n"
             "Program.LSB=100000\n") +
         extra_pal +
         std::string(
             "\n"
             "[hil]\n"
             "CmdOverhead=0\n"
             "CompletionOverhead=0\n"
             "HostBandwidth=512000000000\n") +
         extra_hil;
}

static std::vector<uint64_t> sector_words(uint64_t base) {
  std::vector<uint64_t> words(SECTOR_BEATS * BEAT_WORDS);
  for (size_t i = 0; i < words.size(); i++) {
    words[i] = base + i;
  }
  return words;
}

static void append_words(std::string &bytes, const std::vector<uint64_t> &words) {
  bytes.append(reinterpret_cast<const char *>(words.data()),
               words.size() * sizeof(uint64_t));
}

static std::string write_disk(const std::vector<uint64_t> &sector0,
                              const std::vector<uint64_t> &sector1) {
  std::string bytes;
  append_words(bytes, sector0);
  append_words(bytes, sector1);
  return write_temp_file("blockdev-disk", bytes);
}

static void push_write_sector(blockdev_t &dev,
                              uint32_t tag,
                              const std::vector<uint64_t> &words,
                              uint64_t t_now) {
  for (uint32_t beat = 0; beat < SECTOR_BEATS; beat++) {
    blkdev_data data = {};
    data.tag = tag;
    for (uint32_t word = 0; word < BEAT_WORDS; word++) {
      data.data[word] = words[beat * BEAT_WORDS + word];
    }
    blockdev_test_access::handle_data(dev, data, t_now);
  }
}

static void expect_response_sector(blockdev_t &dev,
                                   const std::vector<uint64_t> &words) {
  std::queue<blkdev_data> &responses = blockdev_test_access::read_responses(dev);
  for (uint32_t beat = 0; beat < SECTOR_BEATS; beat++) {
    assert(!responses.empty());
    const blkdev_data data = responses.front();
    responses.pop();
    for (uint32_t word = 0; word < BEAT_WORDS; word++) {
      assert(data.data[word] == words[beat * BEAT_WORDS + word]);
    }
  }
}

static void test_heap_tie_orders_by_completion_then_seq() {
  std::priority_queue<blkdev_completion,
                      std::vector<blkdev_completion>,
                      blkdev_completion_compare>
      heap;
  heap.push({100, 1, false, 1, {}, false});
  heap.push({100, 0, false, 0, {}, false});
  heap.push({90, 2, false, 2, {}, false});

  assert(heap.top().tag == 2);
  heap.pop();
  assert(heap.top().tag == 0);
  heap.pop();
  assert(heap.top().tag == 1);
}

static void test_read_buffer_survives_later_write() {
  const std::vector<uint64_t> old0 = sector_words(0x1000);
  const std::vector<uint64_t> old1 = sector_words(0x2000);
  const std::vector<uint64_t> new0 = sector_words(0x3000);
  const std::string disk_path = write_disk(old0, old1);
  const std::string config_path =
      write_temp_file("blockdev-ssd-config", ssd_config());

  FakeSimif sim;
  const BLOCKDEVBRIDGEMODULE_struct addrs = mmio_addrs();
  std::vector<std::string> args = {"+blkdev0=" + disk_path,
                                   "+blkdev-ssd-config0=" + config_path};

  {
    blockdev_t dev(sim, addrs, 0, args, 2, 16);

    SsdRequest busy_channel_1;
    busy_channel_1.offset = 1;
    busy_channel_1.len = 1;
    blockdev_test_access::model(dev).submit(busy_channel_1, 0);

    blkdev_request read_req = {};
    read_req.write = false;
    read_req.offset = 0;
    read_req.len = 2;
    read_req.tag = 0;
    blockdev_test_access::schedule_read(dev, read_req, 0);

    blkdev_request write_req = {};
    write_req.write = true;
    write_req.offset = 0;
    write_req.len = 1;
    write_req.tag = 1;
    blockdev_test_access::do_write(dev, write_req);
    push_write_sector(dev, write_req.tag, new0, 1);

    const uint64_t read_done =
        blockdev_test_access::completion_time_for_tag(dev, read_req.tag);
    const uint64_t write_done =
        blockdev_test_access::completion_time_for_tag(dev, write_req.tag);
    assert(write_done < read_done);

    blockdev_test_access::release_ready(dev, write_done);
    assert(!blockdev_test_access::write_acks(dev).empty());
    assert(blockdev_test_access::write_acks(dev).front() == write_req.tag);
    blockdev_test_access::write_acks(dev).pop();

    blockdev_test_access::release_ready(dev, read_done);
    expect_response_sector(dev, old0);
    expect_response_sector(dev, old1);

    blockdev_test_access::flush_file(dev);
  }

  unlink(config_path.c_str());
  unlink(disk_path.c_str());
}

static void test_idle_lifecycle() {
  const std::vector<uint64_t> old0 = sector_words(0x4000);
  const std::vector<uint64_t> old1 = sector_words(0x5000);
  const std::string disk_path = write_disk(old0, old1);
  const std::string config_path =
      write_temp_file("blockdev-ssd-config", ssd_config());

  FakeSimif sim;
  const BLOCKDEVBRIDGEMODULE_struct addrs = mmio_addrs();
  std::vector<std::string> args = {"+blkdev0=" + disk_path,
                                   "+blkdev-ssd-config0=" + config_path};

  {
    blockdev_t dev(sim, addrs, 0, args, 1, 16);
    assert(blockdev_test_access::idle(dev));

    blkdev_request read_req = {};
    read_req.write = false;
    read_req.offset = 0;
    read_req.len = 1;
    read_req.tag = 0;
    blockdev_test_access::schedule_read(dev, read_req, 0);
    assert(!blockdev_test_access::idle(dev));

    const uint64_t read_done =
        blockdev_test_access::completion_time_for_tag(dev, read_req.tag);
    blockdev_test_access::release_ready(dev, read_done - 1);
    assert(!blockdev_test_access::idle(dev));

    blockdev_test_access::release_ready(dev, read_done);
    assert(!blockdev_test_access::idle(dev));

    expect_response_sector(dev, old0);
    assert(blockdev_test_access::idle(dev));
  }

  unlink(config_path.c_str());
  unlink(disk_path.c_str());
}

static void test_write_buffer_fill_ack_has_no_nand_completion() {
  const std::vector<uint64_t> old0 = sector_words(0x8000);
  const std::vector<uint64_t> old1 = sector_words(0x9000);
  const std::vector<uint64_t> new0 = sector_words(0xa000);
  const std::string disk_path = write_disk(old0, old1);
  const std::string config_path = write_temp_file(
      "blockdev-ssd-config",
      ssd_config_with_hil("", "WBEnabled=true\nWBAckLatencyCycles=20000\nHostOverheadCycles=7\n"));

  FakeSimif sim;
  const BLOCKDEVBRIDGEMODULE_struct addrs = mmio_addrs();
  std::vector<std::string> args = {"+blkdev0=" + disk_path,
                                   "+blkdev-ssd-config0=" + config_path};

  {
    blockdev_t dev(sim, addrs, 0, args, 1, 24);

    blkdev_request write_req = {};
    write_req.write = true;
    write_req.offset = 0;
    write_req.len = 1;
    write_req.tag = 0;
    blockdev_test_access::do_write(dev, write_req);
    push_write_sector(dev, write_req.tag, new0, 0);

    assert(blockdev_test_access::completion_count(dev) == 1);
    assert(blockdev_test_access::pending_count(dev) == 0);

    const uint64_t ack_done =
        blockdev_test_access::completion_time_for_tag(dev, write_req.tag);
    assert(ack_done == 20007);

    blockdev_test_access::release_ready(dev, ack_done);
    assert(blockdev_test_access::pending_count(dev) == 0);
    assert(!blockdev_test_access::write_acks(dev).empty());
    assert(blockdev_test_access::write_acks(dev).front() == write_req.tag);
    blockdev_test_access::write_acks(dev).pop();
    assert(blockdev_test_access::write_acks(dev).empty());
    assert(blockdev_test_access::completion_count(dev) == 0);
  }

  unlink(config_path.c_str());
  unlink(disk_path.c_str());
}

static void test_write_buffer_eviction_backpressures_blockdev_ack() {
  const std::vector<uint64_t> old0 = sector_words(0xb000);
  const std::vector<uint64_t> old1 = sector_words(0xc000);
  const std::vector<uint64_t> new0 = sector_words(0xd000);
  const std::vector<uint64_t> new1 = sector_words(0xe000);
  const std::string disk_path = write_disk(old0, old1);
  const std::string config_path = write_temp_file(
      "blockdev-ssd-config",
      ssd_config_with_hil("", "WBEnabled=true\nWBAckLatencyCycles=20000\nHostOverheadCycles=7\nICLCapacityBytes=512\nICLLineSizeBytes=512\n"));

  FakeSimif sim;
  const BLOCKDEVBRIDGEMODULE_struct addrs = mmio_addrs();
  std::vector<std::string> args = {"+blkdev0=" + disk_path,
                                   "+blkdev-ssd-config0=" + config_path};

  {
    blockdev_t dev(sim, addrs, 0, args, 1, 24);

    blkdev_request first = {};
    first.write = true;
    first.offset = 0;
    first.len = 1;
    first.tag = 0;
    blockdev_test_access::do_write(dev, first);
    push_write_sector(dev, first.tag, new0, 0);
    assert(blockdev_test_access::completion_count(dev) == 1);
    assert(blockdev_test_access::pending_count(dev) == 0);
    const uint64_t first_ack =
        blockdev_test_access::completion_time_for_tag(dev, first.tag);
    assert(first_ack == 20007);
    blockdev_test_access::release_ready(dev, first_ack);
    blockdev_test_access::write_acks(dev).pop();

    blkdev_request second = {};
    second.write = true;
    second.offset = 1;
    second.len = 1;
    second.tag = 0;
    blockdev_test_access::do_write(dev, second);
    push_write_sector(dev, second.tag, new1, 0);
    assert(blockdev_test_access::completion_count(dev) == 1);
    assert(blockdev_test_access::pending_count(dev) == 1);

    const uint64_t second_ack =
        blockdev_test_access::completion_time_for_tag(dev, second.tag);
    assert(second_ack == 100002);

    blockdev_test_access::release_ready(dev, 20007);
    assert(blockdev_test_access::write_acks(dev).empty());
    assert(blockdev_test_access::pending_count(dev) == 1);

    blockdev_test_access::release_ready(dev, second_ack);
    assert(blockdev_test_access::pending_count(dev) == 0);
    assert(!blockdev_test_access::write_acks(dev).empty());
    assert(blockdev_test_access::write_acks(dev).front() == second.tag);
    blockdev_test_access::write_acks(dev).pop();
    assert(blockdev_test_access::completion_count(dev) == 0);
  }

  unlink(config_path.c_str());
  unlink(disk_path.c_str());
}

static void test_completion_heap_soft_cap_exits() {
  const pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    const std::vector<uint64_t> old0 = sector_words(0x6000);
    const std::vector<uint64_t> old1 = sector_words(0x7000);
    const std::string disk_path = write_disk(old0, old1);
    const std::string config_path =
        write_temp_file("blockdev-ssd-config", ssd_config());

    FakeSimif sim;
    const BLOCKDEVBRIDGEMODULE_struct addrs = mmio_addrs();
    std::vector<std::string> args = {"+blkdev0=" + disk_path,
                                     "+blkdev-ssd-config0=" + config_path};
    blockdev_t dev(sim, addrs, 0, args, 1, 16);

    blkdev_request first = {};
    first.offset = 0;
    first.len = 1;
    first.tag = 0;
    blockdev_test_access::schedule_read(dev, first, 0);

    blkdev_request second = {};
    second.offset = 1;
    second.len = 1;
    second.tag = 0;
    blockdev_test_access::schedule_read(dev, second, 0);
    _exit(0);
  }

  int status = 0;
  waitpid(pid, &status, 0);
  assert((WIFEXITED(status) && WEXITSTATUS(status) != 0) ||
         WIFSIGNALED(status));
}

} // namespace

int main() {
  test_heap_tie_orders_by_completion_then_seq();
  test_read_buffer_survives_later_write();
  test_idle_lifecycle();
  test_write_buffer_fill_ack_has_no_nand_completion();
  test_write_buffer_eviction_backpressures_blockdev_ack();
  test_completion_heap_soft_cap_exits();
  return 0;
}
