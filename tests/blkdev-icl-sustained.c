#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mmio.h"
#include "blkdev.h"

#ifndef ICL_LINE_SECTORS
#define ICL_LINE_SECTORS 32U
#endif

#ifndef ICL_QD
#define ICL_QD 8U
#endif

#ifndef ICL_SUSTAINED_WRITES
#define ICL_SUSTAINED_WRITES 1024U
#endif

#ifndef ICL_WORKING_SET_PAGES
#define ICL_WORKING_SET_PAGES 4096U
#endif

#define WORDS_PER_LINE ((ICL_LINE_SECTORS * BLKDEV_SECTOR_SIZE) / sizeof(uint64_t))
#define MAX_TAGS 256U

static uint64_t write_buf[ICL_QD][WORDS_PER_LINE] __attribute__((aligned(64)));
static uint64_t issue_cycle[MAX_TAGS];
static unsigned int tag_slot[MAX_TAGS];
static unsigned int slot_busy[ICL_QD];
static uint64_t latencies[ICL_SUSTAINED_WRITES];
static uint32_t rng_state = 0x1234567u;

static int cmp_u64(const void *a, const void *b)
{
  const uint64_t av = *(const uint64_t *) a;
  const uint64_t bv = *(const uint64_t *) b;
  return (av > bv) - (av < bv);
}

static uint32_t next_page(void)
{
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state % ICL_WORKING_SET_PAGES;
}

static unsigned int find_slot(void)
{
  for (unsigned int i = 0; i < ICL_QD; i++) {
    if (!slot_busy[i]) {
      return i;
    }
  }
  return ICL_QD;
}

static void fill_line(unsigned int slot, unsigned int page, unsigned int seq)
{
  for (unsigned int i = 0; i < WORDS_PER_LINE; i++) {
    write_buf[slot][i] =
        ((uint64_t) page << 32) ^ ((uint64_t) seq << 16) ^ (0x51c00000ULL + i);
  }
}

static void poll_completions(unsigned int *completed, unsigned int *inflight)
{
  while (reg_read8(BLKDEV_NCOMPLETE) != 0) {
    const uint64_t end = rdcycle();
    const unsigned int tag = reg_read8(BLKDEV_COMPLETE);
    const unsigned int slot = tag_slot[tag];
    latencies[*completed] = end - issue_cycle[tag];
    (*completed)++;
    (*inflight)--;
    slot_busy[slot] = 0;
  }
}

static void issue_write(unsigned int seq,
                        unsigned int *issued,
                        unsigned int *completed,
                        unsigned int *inflight)
{
  const unsigned int slot = find_slot();
  if (slot == ICL_QD) {
    return;
  }
  while (reg_read8(BLKDEV_NREQUEST) == 0) {
    poll_completions(completed, inflight);
  }

  const unsigned int page = next_page();
  fill_line(slot, page, seq);
  asm volatile("fence" ::: "memory");

  const uint64_t start = rdcycle();
  const unsigned int tag = blkdev_send_request((unsigned long) write_buf[slot],
                                               page * ICL_LINE_SECTORS,
                                               ICL_LINE_SECTORS,
                                               1);
  issue_cycle[tag] = start;
  tag_slot[tag] = slot;
  slot_busy[slot] = 1;
  (*issued)++;
  (*inflight)++;
}

int main(void)
{
  const unsigned int nsectors = blkdev_nsectors();
  const unsigned int max_req_len = blkdev_max_req_len();

  if (max_req_len < ICL_LINE_SECTORS) {
    printf("Error: max_req_len %u < line sectors %u\n",
           max_req_len,
           ICL_LINE_SECTORS);
    return 1;
  }
  if (nsectors <= ICL_WORKING_SET_PAGES * ICL_LINE_SECTORS) {
    printf("Error: blkdev nsectors not large enough: %u <= %u\n",
           nsectors,
           ICL_WORKING_SET_PAGES * ICL_LINE_SECTORS);
    return 1;
  }

  unsigned int issued = 0;
  unsigned int completed = 0;
  unsigned int inflight = 0;
  const uint64_t start = rdcycle();
  while (completed < ICL_SUSTAINED_WRITES) {
    while (issued < ICL_SUSTAINED_WRITES && inflight < ICL_QD) {
      issue_write(issued, &issued, &completed, &inflight);
    }
    poll_completions(&completed, &inflight);
  }
  const uint64_t end = rdcycle();

  qsort(latencies, ICL_SUSTAINED_WRITES, sizeof(latencies[0]), cmp_u64);
  const unsigned int p50_idx = ICL_SUSTAINED_WRITES / 2;
  const unsigned int p99_idx = (ICL_SUSTAINED_WRITES * 99) / 100;

  printf("icl_sustained qd %u writes %u working_set_pages %u elapsed_cycles %lu\n",
         ICL_QD,
         ICL_SUSTAINED_WRITES,
         ICL_WORKING_SET_PAGES,
         (unsigned long) (end - start));
  printf("icl_sustained_p50_cycles %lu\n", (unsigned long) latencies[p50_idx]);
  printf("icl_sustained_p99_cycles %lu\n", (unsigned long) latencies[p99_idx]);
  return 0;
}
