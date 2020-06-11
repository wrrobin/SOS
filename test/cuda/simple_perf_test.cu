#include <stdio.h>
#include <unistd.h>
#include <shmem.h>

#define MIN_MSG_SZ (1<<0)
#define MAX_MSG_SZ (1<<20)
#define NUM_ITER 100
#define WARMUP 10

__global__
void init_data(int n, int *x, int *y, int value)
{
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  int stride = blockDim.x * gridDim.x;
  for (int i = index; i < n; i += stride) {
    x[i] = value * value + value;
    y[i] = value * value + value + 1;
  }
}

#ifdef USE_RDTSC
static inline uint64_t rdtsc()
{
  unsigned int hi, lo;
  __asm__ volatile("rdtsc" : "=a" (lo), "=d" (hi));
  return ((uint64_t)hi << 32) | lo;
}

static inline double GetGHzFreq()
{
  unsigned long long startTime = rdtsc();
  sleep(1);
  unsigned long long endTime = rdtsc();
  return (double)(endTime - startTime) / 1.0e9;
}
#else
double wtime(void)
{
  double wtime = 0.0;

#ifdef CLOCK_MONOTONIC
  struct timespec tv;
  clock_gettime(CLOCK_MONOTONIC, &tv);
  wtime = tv.tv_sec * 1e6;
  wtime += (double)tv.tv_nsec / 1000.0;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  wtime = tv.tv_sec * 1e6;
  wtime += (double)tv.tv_usec;
#endif
  return wtime;
}
#endif

double get_time() {
#ifdef USE_RDTSC
  return rdtsc();
#else
  return wtime();
#endif
}

int main(int argc, char *argv[]) {
  int exitcode = 0;
  int *a, *b;
  int msg_size, i;
  double start_time = 0.0, end_time = 0.0;
#ifdef PROFILE
  double profile_start = 0.0, profile_time = 0.0;
#endif

  shmem_init();
  int me = shmem_my_pe();
  int npes = shmem_n_pes();

  if (me == 0) {
#ifdef USE_DEVICE
    fprintf(stderr, "Device initialization test\n");
#else
    fprintf(stderr, "Host initialization test\n");
#endif
  }

  a = (int *) shmem_malloc(MAX_MSG_SZ * sizeof(int));
  cudaMallocManaged(&b, MAX_MSG_SZ * sizeof(int));

#ifdef USE_RDTSC
  double freq = GetGHzFreq();
#endif

  for (msg_size = MIN_MSG_SZ; msg_size <= MAX_MSG_SZ; msg_size *= 2) {
#ifdef PROFILE
    profile_time = 0.0;
#endif
    for (i = 0; i < (NUM_ITER + WARMUP); i++) {
      shmem_barrier_all();
      if (i == WARMUP && me == 0) start_time = get_time();
#ifdef USE_DEVICE
      int block_size = 256;
      int num_blocks = (msg_size + block_size - 1) / block_size;
      init_data<<<num_blocks, block_size>>>(msg_size, a, b, i);
#ifdef PROFILE
      if (me == 0) profile_start = get_time();
#endif
      cudaDeviceSynchronize();
#ifdef PROFILE
      if (me == 0) profile_time += (get_time() - profile_start);
#endif
#else
      int j;
      for (j = 0; j < msg_size; j++) {
        a[j] = i * i + i;
        b[j] = i * i + i + 1;
      }
#endif
      shmem_barrier_all();
      shmem_int_put(a, b, msg_size, (me + 1) % npes);
    }
    if (me == 0) end_time = get_time();
    shmem_barrier_all();

#ifdef USE_RDTSC
    if (me == 0) { 
      fprintf(stderr, "%10d%10s%10.2f", msg_size, " ", 
              (double)((end_time - start_time) / ((double) NUM_ITER * freq * 1.0e3)));
#ifdef PROFILE
      fprintf(stderr, "%10s%10.2f\n", " ", 
              (double)(profile_time / ((double) NUM_ITER * freq * 1.0e3)));
#else
      fprintf(stderr, "\n");
#endif
    }
#else
    if (me == 0) { 
      fprintf(stderr, "%10d%10s%10.2f", msg_size, " ", 
              (double)((end_time - start_time) / NUM_ITER));
#ifdef PROFILE
      fprintf(stderr, "%10s%10.2f\n", " ", (double)(profile_time / NUM_ITER));
#else
      fprintf(stderr, "\n");
#endif
    }
#endif

    for (i = 0; i < msg_size; i++) {
      if (a[i] != b[i]) {
        fprintf(stderr, "[PE %d] ERROR: expected %d, found %d\n",
                        me, b[i], a[i]);
        exitcode = 1;
        break;
      }
    }

    if (exitcode) break;
  }

  shmem_free(a);
  cudaFree(b);
  shmem_finalize();

  return exitcode;
}
