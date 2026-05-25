#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mmio.h"
#include "blkdev.h"

#ifndef ICL_LINE_SECTORS
#define ICL_LINE_SECTORS 32U
#endif

#ifndef ICL_SUBPAGE_SECTORS
#define ICL_SUBPAGE_SECTORS 8U
#endif

#ifndef ICL_CAPACITY_LINES
#define ICL_CAPACITY_LINES 2048U
#endif

#ifndef ICL_RMW_EVICT_WRITES
#define ICL_RMW_EVICT_WRITES 4U
#endif

#define WORDS_PER_LINE ((ICL_LINE_SECTORS * BLKDEV_SECTOR_SIZE) / sizeof(uint64_t))
#define WORDS_PER_SUBPAGE ((ICL_SUBPAGE_SECTORS * BLKDEV_SECTOR_SIZE) / sizeof(uint64_t))

static uint64_t io_buf[WORDS_PER_LINE] __attribute__((aligned(64)));

static void fill_words(unsigned int page, unsigned int seq, unsigned int words)
{
  for (unsigned int i = 0; i < words; i++) {
    io_buf[i] =
        ((uint64_t) page << 32) ^ ((uint64_t) seq << 16) ^ (0x3d200000ULL + i);
  }
}

static uint64_t write_request(unsigned int page,
                              unsigned int sectors,
                              unsigned int seq)
{
  const unsigned int words =
      (sectors == ICL_LINE_SECTORS) ? WORDS_PER_LINE : WORDS_PER_SUBPAGE;
  fill_words(page, seq, words);
  asm volatile("fence" ::: "memory");

  while (reg_read8(BLKDEV_NREQUEST) == 0) {
  }

  const uint64_t start = rdcycle();
  const unsigned int tag = blkdev_send_request((unsigned long) io_buf,
                                               page * ICL_LINE_SECTORS,
                                               sectors,
                                               1);
  while (reg_read8(BLKDEV_NCOMPLETE) == 0) {
  }
  const uint64_t end = rdcycle();
  const unsigned int done = reg_read8(BLKDEV_COMPLETE);
  if (done != tag) {
    printf("Error: completion tag mismatch page=%u tag=%u done=%u\n",
           page,
           tag,
           done);
    exit(1);
  }
  return end - start;
}

int main(void)
{
  const unsigned int nsectors = blkdev_nsectors();
  const unsigned int max_req_len = blkdev_max_req_len();
  const unsigned int total_pages = ICL_CAPACITY_LINES + ICL_RMW_EVICT_WRITES;
  const unsigned int required = total_pages * ICL_LINE_SECTORS;

  if (max_req_len < ICL_LINE_SECTORS) {
    printf("Error: max_req_len %u < line sectors %u\n",
           max_req_len,
           ICL_LINE_SECTORS);
    return 1;
  }
  if (nsectors <= required) {
    printf("Error: blkdev nsectors not large enough: %u <= %u\n",
           nsectors,
           required);
    return 1;
  }

  printf("icl_rmw_eviction capacity_lines %u evict_writes %u line_sectors %u subpage_sectors %u\n",
         ICL_CAPACITY_LINES,
         ICL_RMW_EVICT_WRITES,
         ICL_LINE_SECTORS,
         ICL_SUBPAGE_SECTORS);
  for (unsigned int page = 0; page < ICL_CAPACITY_LINES; page++) {
    const uint64_t cycles = write_request(page, ICL_LINE_SECTORS, page);
    printf("icl_rmw_eviction_fill page %u cycles %lu\n",
           page,
           (unsigned long) cycles);
  }
  for (unsigned int i = 0; i < ICL_RMW_EVICT_WRITES; i++) {
    const unsigned int page = ICL_CAPACITY_LINES + i;
    const uint64_t cycles =
        write_request(page, ICL_SUBPAGE_SECTORS, ICL_CAPACITY_LINES + i);
    printf("icl_rmw_eviction_subpage expected_victim %u page %u cycles %lu\n",
           i,
           page,
           (unsigned long) cycles);
  }
  return 0;
}
