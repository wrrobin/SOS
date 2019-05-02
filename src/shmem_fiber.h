#ifndef SHMEM_FIBER_H
#define SHMEM_FIBER_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <ucontext.h>

#include "shmem_internal.h"

#define MAX_FIBER_LIMIT 100
#define FIBER_STACK_SIZE 64 * 1024

typedef enum {
    STATUS_INITIALIZED,
    STATUS_READY,
    STATUS_RUNNING,
    STATUS_BLOCKED,
    STATUS_TERMINATED
} fiber_status;

typedef enum {
    POLICY_RANDOM,
    POLICY_RR
} yield_policy;

typedef struct shmem_fiber_t 
{
    int fiber_id;
    ucontext_t uctx;
    fiber_status f_status;
} shmem_fiber_t;

static struct shmem_fiber_t* fibers;
static ucontext_t parent;
static int fiber_count = 0;
static int fibers_initialized = 0;
static int current_running_fiber_index = -1;
static yield_policy y_policy = POLICY_RANDOM;
static int *yield_queue;

static void shuffle_yield_queue(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int usec = tv.tv_usec;
    srand48(usec);

    if (fiber_count > 1) {
        size_t i;
        for (i = fiber_count - 1; i > 0; i--) {
            size_t j = (unsigned int) (drand48() * (i + 1));
            int t = yield_queue[j];
            yield_queue[j] = yield_queue[i];
            yield_queue[i] = t;
        }
    }
}

static int init_yield_queue(void) {
    yield_queue = (int *) malloc (fiber_count * sizeof(int));
    if (yield_queue == NULL) {
        RETURN_ERROR_MSG("Failed to allocate yield queue\n");
        return -1;
    }

    int i;
    for (i = 0; i < fiber_count; i++) {
        yield_queue[i] = i;
    }

    if (y_policy == POLICY_RANDOM) {
        shuffle_yield_queue();
    }

    return 0;
}

static int create_fibers(int count, int yield_policy) {
    if (fibers_initialized) {
        RAISE_WARN_STR("create_fibers: Fibers already initialized");
        return 1;
    }

    fiber_count = count;
    if (fiber_count < 0 || fiber_count > MAX_FIBER_LIMIT) {
        fiber_count = MAX_FIBER_LIMIT;
        RAISE_WARN_MSG("Incorrect fiber count %d passed. "
                       "Total number of fibers is set to %d", 
                       count, fiber_count);
    }

    fibers = (shmem_fiber_t*) malloc (fiber_count * sizeof(shmem_fiber_t));
    if (fibers == NULL) {
        RETURN_ERROR_MSG("Failed to allocate fibers\n");
        return -1;
    }

    int i;
    for (i = 0; i < fiber_count; i++) {
        fibers[i].fiber_id = i;

        if (-1 == getcontext(&fibers[i].uctx)) {
            RETURN_ERROR_MSG("getcontext: Failure to retrieve current context\n");
            return -1;
        }
        fibers[i].uctx.uc_stack.ss_sp = malloc(FIBER_STACK_SIZE);
        if (fibers[i].uctx.uc_stack.ss_sp == NULL) {
            RETURN_ERROR_MSG("Failed to allocate stack for fibers\n");
            return -1;
        }
        fibers[i].uctx.uc_stack.ss_size = FIBER_STACK_SIZE;
        //fibers[i].uctx.uc_link = &fibers[(i + 1) % fiber_count].uctx;
        fibers[i].uctx.uc_link = (i == fiber_count - 1) ? &parent : &fibers[(i + 1) % fiber_count].uctx;
        //fibers[i].uctx.uc_link = &parent;

        fibers[i].f_status = STATUS_INITIALIZED;
    }

    fibers_initialized = 1;

    if (yield_policy < 0 || yield_policy > 1) {
        RAISE_WARN_MSG("Incorrect yield policy mentioned");
        yield_policy = 0;
    }
    y_policy = yield_policy;

    return init_yield_queue();
}

static int init_fiber(int fiber_index, void (*thread_func)(), 
                      int arg1, int arg2) {
    if (!fibers_initialized) {
        RETURN_ERROR_MSG("Fibers are not yet initialized\n");
        return -1;
    }

    if (fiber_index < 0 || fiber_index > (fiber_count - 1)) {
        RETURN_ERROR_MSG("Incorrect fiber argument %d. Total fibers = %d\n", 
                          fiber_index, fiber_count);
        return -1;
    }

    makecontext(&fibers[fiber_index].uctx, (void (*)()) thread_func, 2, arg1, arg2); 
    fibers[fiber_index].f_status = STATUS_READY;

    return 0;
} 

static int start_fiber(int fiber_index) {
    if (!fibers_initialized) {
        RETURN_ERROR_MSG("Fibers are not yet initialized\n");
        return -1;
    }

    if (fiber_index < 0 || fiber_index > (fiber_count - 1)) {
        RETURN_ERROR_MSG("Incorrect fiber argument %d. Total fibers = %d\n",
                          fiber_index, fiber_count);
        return -1;
    }

    if (fibers[fiber_index].f_status != STATUS_READY) {
        RETURN_ERROR_MSG("Fiber %d is not ready to run.\n",
                          fiber_index);
        return -1;
    }

    if (current_running_fiber_index != -1) {
        RETURN_ERROR_MSG("Multiple fibers cannot be started simultaneously.\n");
        return -1;
    }

    fibers[fiber_index].f_status = STATUS_RUNNING;
    current_running_fiber_index = fiber_index;
    if (-1 == swapcontext(&parent, &fibers[fiber_index].uctx)) {
        RETURN_ERROR_MSG("Fiber %d could not be started\n",
                          fiber_index);
        return -1;
    }
    current_running_fiber_index = -1;
    fibers[fiber_index].f_status = STATUS_TERMINATED;
    
    return 0;
}

static int get_next_runnable_index(void) {
    int i, count = 0;

    for (i = 0; i < fiber_count; i++) {
        if (yield_queue[i] == current_running_fiber_index) 
            break;
    }

    i = (i + 1) % fiber_count;
    while(fibers[i].f_status != STATUS_READY) {
        i = (i + 1) % fiber_count;
        count++;
        if (count > fiber_count)
            return -1;
    }

    return i;
}

static int fiber_yield(void) {
    if (!fibers_initialized) {
        RETURN_ERROR_MSG("Fibers are not yet initialized\n");
        return -1;
    }

    if (current_running_fiber_index == -1 || 
        fibers[current_running_fiber_index].f_status != STATUS_RUNNING) {
        RETURN_ERROR_MSG("Undefined behavior. Yield called but no fiber is running\n");
        return -1;
    }

    int old_running_index = current_running_fiber_index;
    int next_index = get_next_runnable_index(); 
    if (next_index == -1) {
        RAISE_WARN_MSG("Fiber %d could not yield. No ready fibers.\n", current_running_fiber_index);
        return -1;
    }
    fibers[current_running_fiber_index].f_status = STATUS_READY;
    current_running_fiber_index = next_index; 
    fibers[current_running_fiber_index].f_status = STATUS_RUNNING;
    if (-1 == swapcontext(&fibers[old_running_index].uctx, &fibers[current_running_fiber_index].uctx)) {
        RAISE_WARN_MSG("Fiber %d could not yield. Error in swapcontext.\n", current_running_fiber_index);
    }

    fibers[current_running_fiber_index].f_status = STATUS_READY;
    current_running_fiber_index = old_running_index;
    fibers[current_running_fiber_index].f_status = STATUS_RUNNING;
    return 0;
}

#endif /* SHMEM_FIBER_H */
