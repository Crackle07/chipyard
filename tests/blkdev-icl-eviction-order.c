#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mmio.h"
#include "blkdev.h"

#ifndef ICL_LINE_SECTORS
#define ICL_LINE_SECTORS 32U
#endif

#ifndef ICL_CAPACITY_LINES
#define ICL_CAPACITY_LINES 2048U
#endif

#define WORDS_PER_LINE ((ICL_LINE_SECTORS * BLKDEV_SECTOR_SIZE) / sizeof(uint64_t))

static uint64_t write_buf[WORDS_PER_LINE] __attribute__((aligned(64)));

static void fill_line(unsigned int page)
{
  for (unsigned int i = 0; i < WORDS_PER_LINE; i++) {
    write_buf[i] = ((uint64_t) page << 32) ^ (0x0e1c7000ULL + i);
  }
}

static uint64_t write_page(unsigned int page)
{
  fill_line(page);
  asm volatile("fence" ::: "memory");

  while (reg_read8(BLKDEV_NREQUEST) == 0) {
  }

  const uint64_t start = rdcycle();
  const unsigned int tag = blkdev_send_request((unsigned long) write_buf,
                                               page * ICL_LINE_SECTORS,
                                               ICL_LINE_SECTORS,
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
  const unsigned int writes = 2U * ICL_CAPACITY_LINES;
  const unsigned int required = writes * ICL_LINE_SECTORS;

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

  printf("icl_eviction_order capacity_lines %u line_sectors %u\n",
         ICL_CAPACITY_LINES,
         ICL_LINE_SECTORS);
  for (unsigned int page = 0; page < ICL_CAPACITY_LINES; page++) {
    const uint64_t cycles = write_page(page);
    printf("icl_eviction_order_fill page %u cycles %lu\n",
           page,
           (unsigned long) cycles);
  }
  for (unsigned int i = 0; i < ICL_CAPACITY_LINES; i++) {
    const unsigned int page = ICL_CAPACITY_LINES + i;
    const uint64_t cycles = write_page(page);
    printf("icl_eviction_order_evict expected_victim %u page %u cycles %lu\n",
           i,
           page,
           (unsigned long) cycles);
  }
  return 0;
}
