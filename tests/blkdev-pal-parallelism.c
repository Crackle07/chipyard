#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmio.h"
#include "blkdev.h"

#define BENCH_REQUESTS 8U
#define BENCH_STRIDE 8U
#define BENCH_PAGE_SECTORS 1U
#define BENCH_SECTORS_PER_REQ 1U
#define WORDS_PER_SECTOR (BLKDEV_SECTOR_SIZE / sizeof(uint64_t))
#define READ_PATTERN_SALT 0x9e3779b97f4a7c15ULL
#define WRITE_PATTERN_SALT 0xd1b54a32d192ed03ULL

static uint64_t write_buf[BENCH_REQUESTS][WORDS_PER_SECTOR]
    __attribute__((aligned(64)));
static uint64_t read_buf[BENCH_REQUESTS][WORDS_PER_SECTOR]
    __attribute__((aligned(64)));

static unsigned int stride_offsets[BENCH_REQUESTS];
static unsigned int contiguous_offsets[BENCH_REQUESTS];

static void fill_offsets(void)
{
  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    contiguous_offsets[i] = i * BENCH_PAGE_SECTORS;
    stride_offsets[i] = i * BENCH_STRIDE * BENCH_PAGE_SECTORS;
  }
}

static void fill_pattern(uint64_t *buf, unsigned int sector, uint64_t salt)
{
  for (unsigned int i = 0; i < WORDS_PER_SECTOR; i++) {
    buf[i] = ((uint64_t) sector << 32) ^ (salt + i);
  }
}

static int check_pattern(const uint64_t *buf, unsigned int sector, uint64_t salt)
{
  for (unsigned int i = 0; i < WORDS_PER_SECTOR; i++) {
    const uint64_t expected = ((uint64_t) sector << 32) ^ (salt + i);
    if (buf[i] != expected) {
      printf("data mismatch sector=%u word=%u got=%lx expected=%lx\n",
             sector,
             i,
             buf[i],
             expected);
      return 0;
    }
  }
  return 1;
}

static void wait_for_completions(unsigned int count)
{
  while (reg_read8(BLKDEV_NCOMPLETE) < count) {
  }
  for (unsigned int i = 0; i < count; i++) {
    (void) reg_read8(BLKDEV_COMPLETE);
  }
}

static void write_one_sector(unsigned int sector, uint64_t *buf)
{
  while (reg_read8(BLKDEV_NREQUEST) == 0) {
  }

  fill_pattern(buf, sector, READ_PATTERN_SALT);
  asm volatile("fence" ::: "memory");
  (void) blkdev_send_request((unsigned long) buf, sector, BENCH_SECTORS_PER_REQ, 1);
  wait_for_completions(1);
}

static void read_one_sector(unsigned int sector, uint64_t *buf)
{
  while (reg_read8(BLKDEV_NREQUEST) == 0) {
  }

  (void) blkdev_send_request((unsigned long) buf, sector, BENCH_SECTORS_PER_REQ, 0);
  wait_for_completions(1);
}

static void init_backing_store(void)
{
  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    write_one_sector(contiguous_offsets[i], write_buf[i]);
  }
  for (unsigned int i = 1; i < BENCH_REQUESTS; i++) {
    write_one_sector(stride_offsets[i], write_buf[i]);
  }
}

static uint64_t run_read_batch(const unsigned int *offsets, uint64_t salt)
{
  memset(read_buf, 0, sizeof(read_buf));
  asm volatile("fence" ::: "memory");

  const uint64_t start = rdcycle();
  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    while (reg_read8(BLKDEV_NREQUEST) == 0) {
    }
    (void) blkdev_send_request((unsigned long) read_buf[i],
                               offsets[i],
                               BENCH_SECTORS_PER_REQ,
                               0);
  }

  wait_for_completions(BENCH_REQUESTS);
  asm volatile("fence" ::: "memory");
  const uint64_t end = rdcycle();

  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    if (!check_pattern(read_buf[i], offsets[i], salt)) {
      exit(1);
    }
  }

  return end - start;
}

static void fill_write_buffers(const unsigned int *offsets, uint64_t salt)
{
  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    fill_pattern(write_buf[i], offsets[i], salt);
  }
  asm volatile("fence" ::: "memory");
}

static void verify_write_batch(const unsigned int *offsets, uint64_t salt)
{
  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    memset(read_buf[i], 0, BLKDEV_SECTOR_SIZE);
    read_one_sector(offsets[i], read_buf[i]);
    if (!check_pattern(read_buf[i], offsets[i], salt)) {
      exit(1);
    }
  }
}

static uint64_t run_write_batch(const unsigned int *offsets, uint64_t salt)
{
  fill_write_buffers(offsets, salt);

  const uint64_t start = rdcycle();
  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    while (reg_read8(BLKDEV_NREQUEST) == 0) {
    }
    (void) blkdev_send_request((unsigned long) write_buf[i],
                               offsets[i],
                               BENCH_SECTORS_PER_REQ,
                               1);
  }

  wait_for_completions(BENCH_REQUESTS);
  asm volatile("fence" ::: "memory");
  const uint64_t end = rdcycle();

  verify_write_batch(offsets, salt);
  return end - start;
}

static void print_ratio(const char *prefix,
                        uint64_t stride_cycles,
                        uint64_t contiguous_cycles)
{
  const uint64_t ratio_milli =
      (stride_cycles * 1000ULL + (contiguous_cycles / 2ULL)) /
      contiguous_cycles;

  printf("%s_stride8_cycles %lu\n", prefix, stride_cycles);
  printf("%s_contiguous_cycles %lu\n", prefix, contiguous_cycles);
  printf("%s_stride8_over_contiguous_ratio %lu.%03lu\n",
         prefix,
         (unsigned long) (ratio_milli / 1000ULL),
         (unsigned long) (ratio_milli % 1000ULL));
}

int main(void)
{
  const unsigned int nsectors = blkdev_nsectors();
  const unsigned int ntags = reg_read8(BLKDEV_NREQUEST);

  fill_offsets();

  if (ntags < BENCH_REQUESTS) {
    printf("Error: benchmark needs %u request tags, only %u available\n",
           BENCH_REQUESTS,
           ntags);
    return 1;
  }

  if (nsectors <= stride_offsets[BENCH_REQUESTS - 1]) {
    printf("Error: blkdev nsectors not large enough: %u <= %u\n",
           nsectors,
           stride_offsets[BENCH_REQUESTS - 1]);
    return 1;
  }

  printf("blkdev PAL benchmark: %u requests, stride=%u, page_sectors=%u\n",
         BENCH_REQUESTS,
         BENCH_STRIDE,
         BENCH_PAGE_SECTORS);
  printf("Experiment A offsets:");
  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    printf(" %u", stride_offsets[i]);
  }
  printf("\n");
  printf("Experiment B offsets:");
  for (unsigned int i = 0; i < BENCH_REQUESTS; i++) {
    printf(" %u", contiguous_offsets[i]);
  }
  printf("\n");
  printf("For Channel=8,Die=1 this is same-channel vs spread-channel.\n");
  printf("For Channel=1,Die=8 this is same-die vs spread-die.\n");

  init_backing_store();

  const uint64_t read_stride_cycles =
      run_read_batch(stride_offsets, READ_PATTERN_SALT);
  const uint64_t read_contiguous_cycles =
      run_read_batch(contiguous_offsets, READ_PATTERN_SALT);
  const uint64_t write_stride_cycles =
      run_write_batch(stride_offsets, WRITE_PATTERN_SALT);
  const uint64_t write_contiguous_cycles =
      run_write_batch(contiguous_offsets, WRITE_PATTERN_SALT);
  const uint64_t read_ratio_milli =
      (read_stride_cycles * 1000ULL + (read_contiguous_cycles / 2ULL)) /
      read_contiguous_cycles;

  print_ratio("read", read_stride_cycles, read_contiguous_cycles);
  print_ratio("write", write_stride_cycles, write_contiguous_cycles);

  printf("stride8_cycles %lu\n", read_stride_cycles);
  printf("contiguous_cycles %lu\n", read_contiguous_cycles);
  printf("stride8_over_contiguous_ratio %lu.%03lu\n",
         (unsigned long) (read_ratio_milli / 1000ULL),
         (unsigned long) (read_ratio_milli % 1000ULL));
  printf("same_channel_cycles %lu\n", read_stride_cycles);
  printf("spread_channel_cycles %lu\n", read_contiguous_cycles);
  printf("same_over_spread_ratio %lu.%03lu\n",
         (unsigned long) (read_ratio_milli / 1000ULL),
         (unsigned long) (read_ratio_milli % 1000ULL));
  printf("Done\n");

  return 0;
}
