#include <stdlib.h>
#include <stdio.h>
#include <shmem.h>
#include <shmemx.h>

long ctr = 0;

int main(void) {
    int me, npes, i;
    long *out, one = 1;
    double t;

    shmem_init();

    me = shmem_my_pe();
    npes = shmem_n_pes();

    out = malloc(sizeof(long) * npes);

    /* Test blocking fetch-add */

    ctr = 0;
    shmem_barrier_all();
    t = shmemx_wtime();

    for (i = 0; i < npes; i++) {
        out[i] = shmem_long_atomic_fetch_add(&ctr, one, i);
    }

    shmem_barrier_all();
    t = shmemx_wtime() - t;

    if (me == 0) printf("fetch_add     %10.2fus\n", t*1000000);

    if (ctr != npes)
        shmem_global_exit(1);

    for (i = 0; i < npes; i++)
        if (!(out[i] >= 0 && out[i] < npes))
            shmem_global_exit(2);

    /* Test NBI fetch-add */

    ctr = 0;
    shmem_barrier_all();
    t = shmemx_wtime();

    for (i = 0; i < npes; i++) {
        shmemx_long_atomic_fetch_add_nbi(&ctr, &one, &out[i], i);
    }

    shmem_barrier_all();
    t = shmemx_wtime() - t;

    if (me == 0) printf("fetch_add_nbi %10.2fus\n", t*1000000);

    if (ctr != npes)
        shmem_global_exit(1);

    for (i = 0; i < npes; i++)
        if (!(out[i] >= 0 && out[i] < npes))
            shmem_global_exit(2);

    shmem_finalize();
    return 0;
}
