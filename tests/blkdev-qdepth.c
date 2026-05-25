#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmio.h"
#include "blkdev.h"

#define MAX_QD 255U
#define BENCH_SECTORS_PER_REQ 1U
#define WORDS_PER_SECTOR (BLKDEV_SECTOR_SIZE / sizeof(uint64_t))
#define READ_SENTINEL 0xdeadbeefcafef00dULL

static const unsigned int qdepths[] = {1U, 2U, 4U, 8U, 16U,
                                       32U, 64U, 128U, 255U};

static uint64_t read_buf[MAX_QD][WORDS_PER_SECTOR] __attribute__((aligned(64)));

static void poison_read_buffers(unsigned int qd)
{
  for (unsigned int sector = 0; sector < qd; sector++) {
    read_buf[sector][0] = READ_SENTINEL;
    read_buf[sector][WORDS_PER_SECTOR - 1] = READ_SENTINEL;
  }
}

static int check_zero_sector(const uint64_t *buf, unsigned int sector)
{
  if (buf[0] != 0) {
    printf("data mismatch sector=%u word=0 got=%lx expected=0\n",
           sector,
           buf[0]);
    return 0;
  }
  if (buf[WORDS_PER_SECTOR - 1] != 0) {
    printf("data mismatch sector=%u word=%u got=%lx expected=0\n",
           sector,
           (unsigned int) (WORDS_PER_SECTOR - 1),
           buf[WORDS_PER_SECTOR - 1]);
    return 0;
  }
  return 1;
}

static void wait_for_available_tag(void)
{
  while (reg_read8(BLKDEV_NREQUEST) == 0) {
  }
}

static void wait_for_completions(unsigned int count)
{
  while (reg_read8(BLKDEV_NCOMPLETE) < count) {
  }
  for (unsigned int i = 0; i < count; i++) {
    (void) reg_read8(BLKDEV_COMPLETE);
  }
}

static uint64_t run_read_qd(unsigned int qd)
{
  poison_read_buffers(qd);
  asm volatile("fence" ::: "memory");

  const uint64_t start = rdcycle();
  for (unsigned int sector = 0; sector < qd; sector++) {
    wait_for_available_tag();
    (void) blkdev_send_request((unsigned long) read_buf[sector],
                               sector,
                               BENCH_SECTORS_PER_REQ,
                               0);
  }
  wait_for_completions(qd);
  asm volatile("fence" ::: "memory");
  const uint64_t end = rdcycle();

  for (unsigned int sector = 0; sector < qd; sector++) {
    if (!check_zero_sector(read_buf[sector], sector)) {
      exit(1);
    }
  }

  return end - start;
}

int main(void)
{
  const unsigned int nsectors = blkdev_nsectors();
  const unsigned int ntags = reg_read8(BLKDEV_NREQUEST);

  if (ntags < MAX_QD) {
    printf("Error: benchmark needs %u visible request tags, only %u available\n",
           MAX_QD,
           ntags);
    return 1;
  }

  if (nsectors < MAX_QD) {
    printf("Error: blkdev nsectors not large enough: %u < %u\n",
           nsectors,
           MAX_QD);
    return 1;
  }

  printf("blkdev QD read benchmark: max_qd=%u sectors_per_req=%u backing=zeros\n",
         MAX_QD,
         BENCH_SECTORS_PER_REQ);

  for (unsigned int i = 0; i < sizeof(qdepths) / sizeof(qdepths[0]); i++) {
    const unsigned int qd = qdepths[i];
    const uint64_t cycles = run_read_qd(qd);
    const uint64_t cycles_per_req_milli =
        (cycles * 1000ULL + (qd / 2ULL)) / qd;
    printf("qd %u read_cycles %lu cycles_per_req %lu.%03lu\n",
           qd,
           cycles,
           (unsigned long) (cycles_per_req_milli / 1000ULL),
           (unsigned long) (cycles_per_req_milli % 1000ULL));
  }

  printf("Done\n");
  return 0;
}
