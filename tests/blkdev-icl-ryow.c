#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mmio.h"
#include "blkdev.h"

#ifndef ICL_LINE_SECTORS
#define ICL_LINE_SECTORS 32U
#endif

#ifndef ICL_RYOW_PAGE
#define ICL_RYOW_PAGE 0U
#endif

#define WORDS_PER_LINE ((ICL_LINE_SECTORS * BLKDEV_SECTOR_SIZE) / sizeof(uint64_t))

static uint64_t io_buf[WORDS_PER_LINE] __attribute__((aligned(64)));

static void fill_line(unsigned int page)
{
  for (unsigned int i = 0; i < WORDS_PER_LINE; i++) {
    io_buf[i] = ((uint64_t) page << 32) ^ (0x1c190000ULL + i);
  }
}

static uint64_t issue(unsigned int page, unsigned char write)
{
  if (write) {
    fill_line(page);
  } else {
    for (unsigned int i = 0; i < WORDS_PER_LINE; i++) {
      io_buf[i] = 0;
    }
  }
  asm volatile("fence" ::: "memory");

  while (reg_read8(BLKDEV_NREQUEST) == 0) {
  }

  const uint64_t start = rdcycle();
  const unsigned int tag = blkdev_send_request((unsigned long) io_buf,
                                               page * ICL_LINE_SECTORS,
                                               ICL_LINE_SECTORS,
                                               write);
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
  const unsigned int required = (ICL_RYOW_PAGE + 1U) * ICL_LINE_SECTORS;

  if (max_req_len < ICL_LINE_SECTORS) {
    printf("Error: max_req_len %u < line sectors %u\n",
           max_req_len,
           ICL_LINE_SECTORS);
    return 1;
  }
  if (nsectors < required) {
    printf("Error: blkdev nsectors not large enough: %u < %u\n",
           nsectors,
           required);
    return 1;
  }

  printf("icl_ryow page %u line_sectors %u\n",
         ICL_RYOW_PAGE,
         ICL_LINE_SECTORS);

  const uint64_t write_cycles = issue(ICL_RYOW_PAGE, 1);
  printf("icl_ryow_write page %u cycles %lu\n",
         ICL_RYOW_PAGE,
         (unsigned long) write_cycles);

  const uint64_t read_cycles = issue(ICL_RYOW_PAGE, 0);
  printf("icl_ryow_read page %u cycles %lu\n",
         ICL_RYOW_PAGE,
         (unsigned long) read_cycles);
  return 0;
}
