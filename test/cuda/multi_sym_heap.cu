#include <stdio.h>
#include <shmem.h>

#define N (1<<20)

__global__
void add(int n, int *x, int *y)
{
  int index = blockIdx.x * blockDim.x + threadIdx.x;
  int stride = blockDim.x * gridDim.x;
  for (int i = index; i < n; i += stride)
    y[i] = x[i] + y[i];
}

int main(int argc, char *argv[]) {
  int exitcode = 0;
  int *a, *b;

  shmem_init();
  int me = shmem_my_pe();
  int npes = shmem_n_pes();

  a = (int *) shmem_malloc(N * sizeof(int));
  cudaMallocManaged(&b, N * sizeof(int));

  for (int i = 0; i < N; i++) {
    a[i] = 1; b[i] = 2;
  }

  int blockSize = 256;
  int numBlocks = (N + blockSize - 1) / blockSize;

  add<<<numBlocks, blockSize>>>(N, a, b);

  cudaDeviceSynchronize();

  shmem_barrier_all();
  shmem_int_put(a, b, N, (me + 1) % npes);
  shmem_barrier_all();

  for (int i = 0; i < N; i++) {
    if (a[i] != b[i]) {
      fprintf(stderr, "[PE %d] ERROR: expected %d, found %d\n", 
                      me, b[i], a[i]);
      exitcode = 1;
    }
  }

  shmem_free(a);
  cudaFree(b);
  shmem_finalize();

  return exitcode;
}
