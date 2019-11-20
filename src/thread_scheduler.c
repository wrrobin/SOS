/* -*- C -*-
 *
 * Copyright 2011 Sandia Corporation. Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S.  Government
 * retains certain rights in this software.
 *
 * Copyright (c) 2018 Intel Corporation. All rights reserved.
 * This software is available to you under the BSD license.
 *
 * This file is part of the Sandia OpenSHMEM software package. For license
 * information, see the LICENSE file in the top level directory of the
 * distribution.
 *
 */


#include "shmem_internal.h"
#include "transport.h"

typedef enum {
    BLOCKED_PUT,
    BLOCKED_GET,
    BLOCKED_WAIT
} fiber_state;

typedef enum {
    POLICY_FIFO,
    POLICY_RANDOM,
    POLICY_AUTO,
    POLICY_NONE
} schedule_policy;

typedef struct shmem_fiber_t {
    uint64_t thread_user_id;
    void *fiber_handle;
    shmem_ctx_t *attached_ctx;
    fiber_state fiber_blocked_reason;
    int is_waiting;
    int is_runnable;
    long *wait_var;
    int wait_cond;
    long wait_val;
    struct shmem_fiber_t *next; 
} shmem_fiber;

schedule_policy q_policy;
static int thread_scheduler_initialized = 0;
uint64_t total_threads;
uint64_t num_shepherds;
uint64_t threads_per_shepherd;
int *thread_priority;
static int vip_thread_state = -1;
uint64_t *registered_threads;

static shmem_fiber **pending_fiber_ll;
uint64_t *pending_fiber_ll_size;

static shmem_fiber** next_runnable;
static uint64_t *current_runnable_thread_count;

int log_verbose;

static inline int get_random_number(int min, int max){
   return min + rand() / (RAND_MAX / (max - min + 1) + 1);
}

void shmem_internal_thread_scheduler_init(uint64_t num_hw_threads, uint64_t num_ul_threads, int *priority_list) {
    if (!ult_scheduling_mode) return;
    log_verbose = shmem_internal_params.THREAD_SCHEDULE_VERBOSE;
    q_policy = shmem_internal_params.THREAD_SCHEDULE_POLICY;

    num_shepherds = num_hw_threads;
    total_threads = num_ul_threads;
    threads_per_shepherd = num_ul_threads / num_hw_threads;
    thread_priority = priority_list;
    vip_thread_state = thread_priority[0] == 2 ? 0 : (thread_priority[1] == 2 ? 1 : (thread_priority[2] == 2 ? 2 : -1));

    if (log_verbose && shmem_internal_my_pe == 0) {
        fprintf(stderr, "Number of shepherds %lu, number of ults %lu\n", num_shepherds, total_threads);
        fprintf(stderr, "Thread priority set to: PUT: %d, GET: %d, WAIT: %d\n", thread_priority[0], thread_priority[1], thread_priority[2]);
        fprintf(stderr, "The highest priority thread state is %d\n", vip_thread_state);
    }

    pending_fiber_ll = (shmem_fiber **) calloc(num_shepherds, sizeof(shmem_fiber *));
    pending_fiber_ll_size = (uint64_t *) calloc(num_shepherds, sizeof(uint64_t));

    next_runnable = (shmem_fiber **) calloc(num_shepherds, sizeof(shmem_fiber *));
    current_runnable_thread_count = (uint64_t *) calloc(num_shepherds, sizeof(uint64_t));
    registered_threads = (uint64_t *) calloc(num_shepherds, sizeof(uint64_t));

    thread_scheduler_initialized = 1;
}

void shmem_internal_thread_scheduler_finalize(void) {
    if (!ult_scheduling_mode) return;
    thread_scheduler_initialized = 0;

    for (int i = 0; i < num_shepherds; i++) { 
        if (pending_fiber_ll[i] != NULL)
            pending_fiber_ll[i] = NULL;
        if (next_runnable[i] != NULL)
            next_runnable[i] = NULL;
    }

    free(pending_fiber_ll);
    free(pending_fiber_ll_size);
    free(next_runnable);
    free(current_runnable_thread_count);
    free(registered_threads);

    num_shepherds = 0;
    total_threads = 0;
}

void shmem_internal_thread_scheduler_register(void) {
    if (!ult_scheduling_mode) return;
    uint64_t caller_tid;
    int shepherd;
    shmem_internal_getultinfo_fn(&shepherd, &caller_tid);

    registered_threads[shepherd]++;
    if (log_verbose && shmem_internal_my_pe == 0) fprintf(stderr, "Total registered threads %lu and attending threads %lu\n", registered_threads[shepherd], threads_per_shepherd);
}

static inline void append(int shepherd, shmem_fiber* temp) {
    if (pending_fiber_ll[shepherd] == NULL) {
        pending_fiber_ll[shepherd] = temp;
    } else {
        shmem_fiber *last = pending_fiber_ll[shepherd];
        while (last->next != NULL) {
            last = last->next;
        }
        last->next = temp;
    }
    pending_fiber_ll_size[shepherd]++;
}

static inline void display_ll(int shepherd, uint64_t update, int op) {
    shmem_fiber *temp = pending_fiber_ll[shepherd];
    if (op == 1) fprintf(stderr, "[SHPHRD %d] Deleted value %lu\n", shepherd, update);
    else if (op == 2) fprintf(stderr, "[SHPHRD %d] Updated value %lu\n", shepherd, update);
    else if (op == 3) fprintf(stderr, "[SHPHRD %d] Chose value %lu\n", shepherd, update);
    else fprintf(stderr, "[SHPHRD %d] Added value %lu\n", shepherd, update);
    fprintf(stderr, "[SHPHRD %d] LL: ", shepherd);
    while (temp != NULL) {
        fprintf(stderr, "%lu->", temp->thread_user_id);
        temp = temp->next;
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

static int shmem_internal_add_to_thread_queue(shmem_ctx_t *ctx, int reason, int shepherd, uint64_t tid, 
                                              long *var, int cond, long value) {
    if (shmem_internal_get_thread_handle_fn == NULL) {
        fprintf(stderr, "Unable to get thread handle as such function is " 
                        "not registered\n");
        return -1;
    }

    if (q_policy == POLICY_NONE) { return -1; }

    int tid_exists = 0;

    shmem_fiber *temp = pending_fiber_ll[shepherd], *prev_temp = NULL;
    while (temp != NULL) {
        if (temp->thread_user_id == tid) {
            temp->attached_ctx = ctx;
            temp->fiber_blocked_reason = reason;
            if (var != NULL) {
                temp->wait_var = var;
                temp->wait_cond = cond;
                temp->wait_val = value;
            }
            temp->is_waiting = 1;
            temp->is_runnable = 0;
            tid_exists = 1;
            // Moving the item at the end of the queue
            if (temp->next != NULL) {
                if (prev_temp == NULL) {
                    pending_fiber_ll[shepherd] = temp->next;
                } else {
                    prev_temp->next = temp->next;
                }
                shmem_fiber *last = pending_fiber_ll[shepherd];
                while (last->next != NULL) {
                    last = last->next;
                }
                last->next = temp;
                temp->next = NULL;
            }
            break;
        }
        prev_temp = temp;
        temp = temp->next;
    }

    if (!tid_exists) {
        shmem_fiber *temp = (shmem_fiber *) malloc(sizeof(shmem_fiber));
        temp->thread_user_id = tid;
        temp->fiber_handle = shmem_internal_get_thread_handle_fn();
        temp->attached_ctx = ctx;
        temp->fiber_blocked_reason = reason;
        if (var != NULL) {
            temp->wait_var = var;
            temp->wait_cond = cond;
            temp->wait_val = value;
        }
        temp->is_waiting = 1;
        temp->is_runnable = 0;
        temp->next = NULL;

        append(shepherd, temp);

        if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, tid, 0);
    } else {
        if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, tid, 2);
    }
    return 0;
}

void shmem_internal_remove_from_thread_queue(void) {
    if (!ult_scheduling_mode) return;
    if (shmem_internal_getultinfo_fn == NULL) {
        fprintf(stderr, "Unable to get thread id as such function is "
                        "not registered\n");
        return;
    }

    uint64_t tid;
    int shepherd;
    shmem_internal_getultinfo_fn(&shepherd, &tid);

    shmem_fiber *ret = pending_fiber_ll[shepherd], *prev_ret = NULL;
    while (ret != NULL) {
        if (ret->thread_user_id == tid) {
            if (prev_ret == NULL)
                pending_fiber_ll[shepherd] = ret->next;
            else
                prev_ret->next = ret->next;
            if (ret->is_runnable) current_runnable_thread_count[shepherd]--;
            pending_fiber_ll_size[shepherd]--;
            free(ret);
            break;
        }
        prev_ret = ret;
        ret = ret->next;
    }
    if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, tid, 1);
}

#define COMP(type, a, b, ret)                            \
    do {                                                 \
        ret = 0;                                         \
        switch (type) {                                  \
        case SHMEM_CMP_EQ:                               \
            if (a == b) ret = 1;                         \
            break;                                       \
        case SHMEM_CMP_NE:                               \
            if (a != b) ret = 1;                         \
            break;                                       \
        case SHMEM_CMP_GT:                               \
            if (a > b) ret = 1;                          \
            break;                                       \
        case SHMEM_CMP_GE:                               \
            if (a >= b) ret = 1;                         \
            break;                                       \
        case SHMEM_CMP_LT:                               \
            if (a < b) ret = 1;                          \
            break;                                       \
        case SHMEM_CMP_LE:                               \
            if (a <= b) ret = 1;                         \
            break;                                       \
        default:                                         \
            RAISE_ERROR(-1);                             \
        }                                                \
    } while(0)


int shmem_internal_runnable_thread_exists(shmem_ctx_t *ctx, int reason, long *var, int cond, long value) {
    if (!thread_scheduler_initialized) return SCHEDULER_RET_CODE_UNINITIALIZED;
    if (shmem_internal_getultinfo_fn == NULL) {
        fprintf(stderr, "Unable to get thread id as such function is "
                        "not registered\n");
        return SCHEDULER_RET_CODE_UNINITIALIZED;
    }

    uint64_t caller_tid;
    int shepherd;
    shmem_internal_getultinfo_fn(&shepherd, &caller_tid);

    if (thread_priority[reason] != 0)
        shmem_internal_add_to_thread_queue(ctx, reason, shepherd, caller_tid, var, cond, value);

    if (pending_fiber_ll[shepherd] == NULL) {
        return SCHEDULER_RET_CODE_QUEUE_EMPTY;
    }

    if (registered_threads[shepherd] != threads_per_shepherd) {
        return SCHEDULER_RET_CODE_ALL_THREADS_NOT_STARTED;
    }

    int ret_code = SCHEDULER_RET_CODE_NO_RUNNABLE_THREADS;
    shmem_fiber *queue = pending_fiber_ll[shepherd];

    if (q_policy == POLICY_AUTO) {
        if (current_runnable_thread_count[shepherd] > 1) {
            if (vip_thread_state != -1) {
                shmem_fiber *first_non_vip_runnable = NULL;
                while (queue != NULL) {
                    if (queue->thread_user_id != caller_tid && queue->is_runnable == 1) {
                        if (queue->fiber_blocked_reason == vip_thread_state) {
                            next_runnable[shepherd] = queue;
                            return queue->thread_user_id;
                        } else {
                            if (first_non_vip_runnable == NULL) 
                                first_non_vip_runnable = queue;
                        }
                    }
                    queue = queue->next;
                }
                if (first_non_vip_runnable) {
                    next_runnable[shepherd] = first_non_vip_runnable;
                    return first_non_vip_runnable->thread_user_id;
                }
            } else {
                while (queue != NULL) {
                    if (queue->thread_user_id != caller_tid && queue->is_runnable == 1) {
                        next_runnable[shepherd] = queue;
                        return queue->thread_user_id;
                    }
                    queue = queue->next;
                }
            }
        }

        current_runnable_thread_count[shepherd] = 0;
        while (queue != NULL) {
            if (queue->thread_user_id != caller_tid && queue->is_waiting == 1) {
                if (queue->fiber_blocked_reason == BLOCKED_PUT) {
                    uint64_t c_cntr_val = shmem_transport_pcntr_get_completed_write((shmem_transport_ctx_t *) queue->attached_ctx);
                    uint64_t p_cntr_val = shmem_transport_pcntr_get_issued_write((shmem_transport_ctx_t *) queue->attached_ctx);
                    if (c_cntr_val == p_cntr_val) {
                        queue->is_runnable = 1;
                        current_runnable_thread_count[shepherd]++;
                        next_runnable[shepherd] = queue;
                        if (ret_code == SCHEDULER_RET_CODE_NO_RUNNABLE_THREADS) ret_code = queue->thread_user_id;
                    }
                } else if (queue->fiber_blocked_reason == BLOCKED_GET) {
                    uint64_t c_cntr_val = shmem_transport_pcntr_get_completed_read((shmem_transport_ctx_t *) queue->attached_ctx);
                    uint64_t p_cntr_val = shmem_transport_pcntr_get_issued_read((shmem_transport_ctx_t *) queue->attached_ctx);
                    if (c_cntr_val == p_cntr_val) {
                        queue->is_runnable = 1;
                        current_runnable_thread_count[shepherd]++;
                        next_runnable[shepherd] = queue;
                        if (ret_code == SCHEDULER_RET_CODE_NO_RUNNABLE_THREADS) ret_code = queue->thread_user_id;
                    }
                } else if (queue->fiber_blocked_reason == BLOCKED_WAIT) {
                    int cmpret;
                    COMP(queue->wait_cond, *(queue->wait_var), queue->wait_val, cmpret);
                    if (cmpret) {
                        queue->is_runnable = 1;
                        current_runnable_thread_count[shepherd]++;
                        next_runnable[shepherd] = queue;
                        if (ret_code == SCHEDULER_RET_CODE_NO_RUNNABLE_THREADS) ret_code = queue->thread_user_id;
                    } 
                }
            }
            queue = queue->next;
        }
    } else if (q_policy == POLICY_FIFO || q_policy == POLICY_RANDOM) {
        if (pending_fiber_ll_size[shepherd] > 1) {
            return pending_fiber_ll[shepherd]->thread_user_id;
        }
    }
    return ret_code;
}

int shmem_internal_get_next_thread(void **next_thread) {
    if (!thread_scheduler_initialized) return SCHEDULER_RET_CODE_UNINITIALIZED;
    if (shmem_internal_getultinfo_fn == NULL) {
        fprintf(stderr, "Unable to get thread id as such function is "
                        "not registered\n");
        return SCHEDULER_RET_CODE_UNINITIALIZED;
    }

    uint64_t caller_tid;
    int shepherd;
    shmem_internal_getultinfo_fn(&shepherd, &caller_tid);

    if (pending_fiber_ll[shepherd] == NULL) {
        *next_thread = NULL;
        return SCHEDULER_RET_CODE_QUEUE_EMPTY;
    }

    if (registered_threads[shepherd] != threads_per_shepherd) {
        return SCHEDULER_RET_CODE_ALL_THREADS_NOT_STARTED; 
    }

    int ret_code = SCHEDULER_RET_CODE_NO_RUNNABLE_THREADS;
    uint64_t ret_tid = 0;

    if (q_policy == POLICY_AUTO) {
        if (next_runnable[shepherd] != NULL) {
            *next_thread = next_runnable[shepherd]->fiber_handle;
            ret_tid = next_runnable[shepherd]->thread_user_id;
            next_runnable[shepherd]->is_waiting = 0;
            next_runnable[shepherd]->is_runnable = 0;
            current_runnable_thread_count[shepherd]--;
            ret_code = ret_tid; 
            if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, ret_tid, 3);
            return ret_code;
        }

        shmem_fiber *ret = pending_fiber_ll[shepherd];

        while (ret != NULL) {
            if (ret->is_runnable == 1 && ret->thread_user_id != caller_tid) {
                *next_thread = ret->fiber_handle;
                ret_tid = ret->thread_user_id;
                ret->is_waiting = 0;
                ret->is_runnable = 0;
                current_runnable_thread_count[shepherd]--;
                ret_code = ret_tid;
                break;
            }
            ret = ret->next;
        }
    } else if (q_policy == POLICY_FIFO) {
        shmem_fiber *queue = pending_fiber_ll[shepherd];
        if (queue->next != NULL) {
            return queue->thread_user_id;
        }
    } else if (q_policy == POLICY_RANDOM) {
        srand(time(0));
        int rand_number = get_random_number(0, pending_fiber_ll_size[shepherd] - 1);
 
        shmem_fiber *queue = pending_fiber_ll[shepherd];
        while (queue != NULL) {
            if (rand_number == 0)
                return queue->thread_user_id;
            rand_number--;
            queue = queue->next;
        }
    }

    if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, ret_tid, 3);

    return ret_code;
}


