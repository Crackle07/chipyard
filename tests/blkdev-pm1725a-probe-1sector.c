#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmio.h"
#include "blkdev.h"

#define PROBE_SECTORS 1U
#define PROBE_BYTES (PROBE_SECTORS * BLKDEV_SECTOR_SIZE)
#define PROBE_WORDS (PROBE_BYTES / sizeof(uint64_t))

static uint64_t read_buf[PROBE_WORDS] __attribute__((aligned(64)));

static void wait_for_available_tag(void)
{
  while (reg_read8(BLKDEV_NREQUEST) == 0) {
  }
}

int main(void)
{
  const unsigned int nsectors = blkdev_nsectors();
  const unsigned int max_req_len = blkdev_max_req_len();

  if (nsectors < PROBE_SECTORS) {
    printf("Error: blkdev nsectors not large enough: %u < %u\n",
           nsectors,
           PROBE_SECTORS);
    return 1;
  }
  if (max_req_len < PROBE_SECTORS) {
    printf("Error: blkdev max_req_len not large enough: %u < %u\n",
           max_req_len,
           PROBE_SECTORS);
    return 1;
  }

  memset(read_buf, 0xa5, sizeof(read_buf));
  asm volatile("fence" ::: "memory");

  wait_for_available_tag();
  (void) blkdev_send_request((unsigned long) read_buf, 0, PROBE_SECTORS, 0);
  asm volatile("fence" ::: "memory");
  const uint64_t start = rdcycle();
  while (reg_read8(BLKDEV_NCOMPLETE) < 1) {
  }
  const uint64_t end = rdcycle();
  asm volatile("fence" ::: "memory");

  (void) reg_read8(BLKDEV_COMPLETE);

  for (unsigned int i = 0; i < PROBE_WORDS; i++) {
    if (read_buf[i] != 0) {
      printf("Error: data mismatch word=%u got=%lx expected=0\n",
             i,
             read_buf[i]);
      return 1;
    }
  }

  printf("pm1725a_probe_cycles %lu\n", (unsigned long) (end - start));
  return 0;
}
