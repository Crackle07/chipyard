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

#ifndef ICL_RMW_WRITES
#define ICL_RMW_WRITES 16U
#endif

#define WORDS_PER_SUBPAGE ((ICL_SUBPAGE_SECTORS * BLKDEV_SECTOR_SIZE) / sizeof(uint64_t))

static uint64_t write_buf[WORDS_PER_SUBPAGE] __attribute__((aligned(64)));

static void fill_subpage(unsigned int page, unsigned int seq)
{
  for (unsigned int i = 0; i < WORDS_PER_SUBPAGE; i++) {
    write_buf[i] =
        ((uint64_t) page << 32) ^ ((uint64_t) seq << 16) ^ (0x3d000000ULL + i);
  }
}

static uint64_t write_subpage(unsigned int page, unsigned int seq)
{
  fill_subpage(page, seq);
  asm volatile("fence" ::: "memory");

  while (reg_read8(BLKDEV_NREQUEST) == 0) {
  }

  const uint64_t start = rdcycle();
  const unsigned int tag = blkdev_send_request((unsigned long) write_buf,
                                               page * ICL_LINE_SECTORS,
                                               ICL_SUBPAGE_SECTORS,
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
  const unsigned int required = ICL_RMW_WRITES * ICL_LINE_SECTORS;

  if (max_req_len < ICL_SUBPAGE_SECTORS) {
    printf("Error: max_req_len %u < subpage sectors %u\n",
           max_req_len,
           ICL_SUBPAGE_SECTORS);
    return 1;
  }
  if (nsectors <= required) {
    printf("Error: blkdev nsectors not large enough: %u <= %u\n",
           nsectors,
           required);
    return 1;
  }

  printf("icl_rmw_cold writes %u line_sectors %u subpage_sectors %u\n",
         ICL_RMW_WRITES,
         ICL_LINE_SECTORS,
         ICL_SUBPAGE_SECTORS);
  for (unsigned int page = 0; page < ICL_RMW_WRITES; page++) {
    const uint64_t cycles = write_subpage(page, page);
    printf("icl_rmw_cold_write page %u cycles %lu\n",
           page,
           (unsigned long) cycles);
  }
  return 0;
}
