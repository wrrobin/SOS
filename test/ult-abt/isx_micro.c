#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

#include <unistd.h>
#include <stdint.h>

#include <shmem.h>
#include <shmemx.h>

#include <abt.h>

#define NUM_DATA_POINTS 17
#define MAX_PUT_MSG_SZ (1<<NUM_DATA_POINTS)
//#define MAX_PUT_MSG_SZ (65536)
//#define MAX_PUT_MSG_SZ (512)

int me, npes;
long *src, *dest, *fadd_index, fadd_fetch;
int first_error_index, expected_value, observed_value;
int iterations;
int nxstreams, nthreads;

shmem_ctx_t *ctx_pool;
ABT_thread *abt_threads;

struct thread_args {
    int tid;
    int msg_size;
};

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

int init_src_dest_index() {

    src = (long *) malloc(MAX_PUT_MSG_SZ * sizeof (long));
    if (src == NULL) {
        fprintf(stderr, "Allocation of source data object failed\n");
        return -1;
    }
    dest = (long *) shmem_malloc(npes * MAX_PUT_MSG_SZ * sizeof (long));
    if (dest == NULL) {
        fprintf(stderr, "Allocation of symmetric destination data object "
                        "failed\n");
        return -1;
    }
    fadd_index = (long *) shmem_malloc (npes * sizeof (long));
    if (fadd_index == NULL) {
        fprintf(stderr, "Allocation of fadd index data object failed\n");
        return -1;
    }

    return 0;
}

void finalize_src_data_index() {
    shmem_free(fadd_index);
    shmem_free(dest);
    free(src);
}

void fill_data(int size) {
    
    int i;
    for (i = 0; i < size; i++) {
        src[i] = (me + i) % size;
    }
    for (i = 0; i < size * npes; i++) {
        dest[i] = 0;
    }
    for (i = 0; i < npes; i++) {
        fadd_index[i] = -1;
    }
    fadd_fetch = 0;
}

static void threaded_execute(void *arg) {
    int i, j;
    struct thread_args * t_args = (struct thread_args *) arg;
    int thread_id = t_args->tid;
    int size = t_args->msg_size;

    shmemx_thread_register();

    for (i = 0; i < npes; ++i) {
        if (i % (nthreads * nxstreams) == thread_id) {
            const long write_offset = shmem_ctx_long_atomic_fetch_add(ctx_pool[thread_id], &fadd_fetch, size, i);
            shmem_ctx_long_put(ctx_pool[thread_id], &dest[write_offset], &src[0], size, i);
        }
    }

    shmemx_thread_unregister();
}

int verify_result(int size) {
    int i, j;
    long pivot;
    
    for (i = 0; i < npes; i++) {
        pivot = dest[i * size];
        for (j = i * size; j < (i + 1) * size; j++) {
            if (dest[j] != (pivot % size)) {
                first_error_index = j;
                expected_value = pivot % size;
                observed_value = dest[j];
                return 1;
            }
            pivot++;
        }
    }

    return 0;
}

int get_thread_index(ABT_thread abt_thread) {
    ABT_bool is_same;
    for (int i = 0; i < nthreads; i++) {
        ABT_thread_equal(abt_threads[i], abt_thread, &is_same);
        if (is_same == ABT_TRUE) {
            return i;
        }
    }
    return -1;
}

void my_get_ultinfo(int *xstream, uint64_t *tid) {
    ABT_thread_id abt_tid;
    ABT_thread self;
    ABT_thread_self(&self);
    ABT_thread_self_id(&abt_tid);
    *tid = abt_tid;

    int pool_id;
    ABT_thread_get_last_pool_id(self, &pool_id);
    *xstream = pool_id;
}

static void my_yield(int check) {

    if (check == 0) {
        ABT_thread *next_thr;
        ABT_thread_state state;
        int ret = shmemx_get_next_thread((void **) &next_thr);
        if (ret >= 0) {
            ABT_thread_get_state(*next_thr, &state);
            if (state == ABT_THREAD_STATE_READY) {
                ABT_thread_yield_to(*next_thr);
            }
            else { 
                ABT_thread_yield();
            }
        }
    } else {
        ABT_thread_yield();
    }
}

void *my_get_thread_handle(void) {
    ABT_thread *self = malloc (sizeof(ABT_thread));
    ABT_thread_self(self);
    return (void *) self;
}

int main(int argc, char *argv[]) {
    int tl, ret;
    int msg_size, iter;
    double start_time, total_time;
    int i;

    if (argc == 3) {
      nxstreams = atoi(argv[1]);
      nthreads = atoi(argv[2]);
    } else if (argc == 2) {
      nxstreams = 1;
      nthreads = atoi(argv[1]);
    } else {
      nxstreams = 1;
      nthreads = 2;
    }

    struct thread_args *t_arg;

    t_arg = (struct thread_args *) malloc (nxstreams * nthreads * sizeof (struct thread_args));

    ABT_xstream *abt_xstreams;
    ABT_pool *abt_pools;

    abt_xstreams = (ABT_xstream *) malloc (nxstreams * sizeof (ABT_xstream));
    abt_pools = (ABT_pool *) malloc (nxstreams * sizeof (ABT_pool));


    abt_threads = (ABT_thread *) malloc (nxstreams * nthreads * sizeof (ABT_thread));

    shmemx_register_yield(&my_yield);
    shmemx_register_get_thread_handle(&my_get_thread_handle);
    shmemx_register_getultinfo(&my_get_ultinfo);

    ret = shmem_init_thread(SHMEM_THREAD_MULTIPLE, &tl);

    if (tl != SHMEM_THREAD_MULTIPLE || ret != 0) {
        printf("Init failed (requested thread level %d, got %d, ret %d)\n",
               SHMEM_THREAD_MULTIPLE, tl, ret);
        if (ret == 0) {
            shmem_global_exit(1);
        } else {
            return ret;
        }
    }

//    shmem_init();

    me = shmem_my_pe();
    npes = shmem_n_pes();
    if (me == 0) fprintf(stderr, "Total PEs %d\n", npes);
    ret = init_src_dest_index();
    if (ret == -1) {
        shmem_global_exit(1);
    }

    ctx_pool = (shmem_ctx_t *) malloc (nxstreams * nthreads * sizeof (shmem_ctx_t));
    for (i = 0; i < nxstreams * nthreads; i++) {
        shmem_ctx_create(SHMEM_CTX_PRIVATE, &ctx_pool[i]);
    }

    if (me == 0)
        fprintf(stderr, "Number of threads = %d\n", nthreads * nxstreams);

    fflush(stderr);
    ABT_init(argc, argv);


    ABT_xstream_self(&abt_xstreams[0]);
    for (i = 1; i < nxstreams; i++) {
        ABT_xstream_create(ABT_SCHED_NULL, &abt_xstreams[i]);
    }
    for (i = 0; i < nxstreams; i++) {
        ABT_xstream_get_main_pools(abt_xstreams[i], 1, &abt_pools[i]);
    }

    int thread_priority[] = {1, 2, 0};

    double freq = GetGHzFreq();
    for (msg_size = 1; msg_size <= MAX_PUT_MSG_SZ; msg_size *= 2) {
        total_time = 0.0;
        if (msg_size <= 8192)
            iterations = 1000;
        else if (msg_size >= 8192 && msg_size <= 65536)
            iterations = 200;
        else
            iterations = 100;

        for (iter = 0; iter < iterations; iter++) {
            shmemx_thread_scheduler_init(nxstreams, nxstreams * nthreads, thread_priority);
            fill_data(msg_size);
            shmem_barrier_all();
            if (me == 0) start_time = rdtsc();
            for (i = 0; i < nxstreams * nthreads; i++) {
                t_arg[i].tid = i;
                t_arg[i].msg_size = msg_size;
                ABT_thread_create(abt_pools[i % nxstreams], threaded_execute, 
                                  (struct thread_args *) &t_arg[i], 
                                  ABT_THREAD_ATTR_NULL, &abt_threads[i]);
            }
            for (i = 0; i < nxstreams * nthreads; i++) {
                ABT_thread_join(abt_threads[i]);
            }
            shmem_barrier_all();
            if (me == 0) total_time += (rdtsc() - start_time);
            for (i = 0; i < nxstreams * nthreads; i++) {
                ABT_thread_free(&abt_threads[i]);
            }
            shmemx_thread_scheduler_finalize();
        }
   
        shmem_barrier_all();
 
        if (verify_result(msg_size)) {
            fprintf(stderr, "[PE %d]: Incorrect result observed at index %d: "
                            "Expected value: %ld, Observed value: %ld\n", me, 
                            first_error_index, expected_value, observed_value);
        }

        if (me == 0) {
            double avg_latency = (double) total_time / ((double) iterations * freq * 1.0e3);
            fprintf(stderr, "Size: %10ld Avg latency = %10.2lf us\n",
                                msg_size, avg_latency);
        }
        shmem_barrier_all();
    }

    for (i = 1; i < nxstreams; i++) {
        ABT_xstream_join(abt_xstreams[i]);
        ABT_xstream_free(&abt_xstreams[i]);
    }

    for (i = 0; i < nxstreams * nthreads; i++) {
        shmem_ctx_destroy(ctx_pool[i]);
    }


    free(ctx_pool);
    free(t_arg);
    free(abt_threads);

    ABT_finalize();

    finalize_src_data_index();
    shmem_finalize();
    return 0;
}
