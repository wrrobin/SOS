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
    BLOCKED_TARGET,
    UNKNOWN
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
    uint64_t fiber_blocked_cnt;
    uint64_t fiber_blocked_value;
    int is_waiting;
    int is_runnable;
    struct shmem_fiber_t *next; 
} shmem_fiber;

schedule_policy q_policy = POLICY_AUTO;
static int thread_scheduler_initialized = 0;
uint64_t total_threads;
uint64_t num_shepherds;

static shmem_fiber **pending_fiber_ll;
uint64_t *pending_fiber_ll_size;

static uint64_t **thread_attendance;
static shmem_fiber** next_runnable;
static int *are_all_threads_started;
static uint64_t *current_runnable_thread_count;

int log_verbose;

void shmem_internal_thread_scheduler_init(uint64_t num_hw_threads, uint64_t num_ul_threads) {
    log_verbose = shmem_internal_params.THREAD_SCHEDULE_VERBOSE;

    num_shepherds = num_hw_threads;
    total_threads = num_ul_threads;

    if (log_verbose && shmem_internal_my_pe == 0) fprintf(stderr, "Number of shepherds %lu, number of ults %lu\n", num_shepherds, total_threads);

    pending_fiber_ll = (shmem_fiber **) calloc(num_shepherds, sizeof(shmem_fiber *));
    pending_fiber_ll_size = (uint64_t *) calloc(num_shepherds, sizeof(uint64_t));

    thread_attendance = (uint64_t **) malloc(num_shepherds * sizeof(uint64_t *));
    uint64_t thread_per_shepherd = total_threads / num_shepherds; // For now, must be divisible TODO: handle the unbalanced case
    for (int i = 0; i < num_shepherds; i++) {
        thread_attendance[i] = (uint64_t *) malloc(thread_per_shepherd * sizeof(uint64_t));
        for (int j = 0; j < thread_per_shepherd; j++) 
            thread_attendance[i][j] = INT_MAX;
    }

    next_runnable = (shmem_fiber **) calloc(num_shepherds, sizeof(shmem_fiber *));
    are_all_threads_started = (int *) calloc(num_shepherds, sizeof(int));
    current_runnable_thread_count = (uint64_t *) calloc(num_shepherds, sizeof(uint64_t));

    thread_scheduler_initialized = 1;
}

void shmem_internal_thread_scheduler_finalize(void) {
    thread_scheduler_initialized = 0;

    for (int i = 0; i < num_shepherds; i++) { 
        if (pending_fiber_ll[i] != NULL)
            pending_fiber_ll[i] = NULL;
        if (next_runnable[i] != NULL)
            next_runnable[i] = NULL;
        free(thread_attendance[i]);
    }

    free(pending_fiber_ll);
    free(pending_fiber_ll_size);
    free(thread_attendance);
    free(next_runnable);
    free(are_all_threads_started);
    free(current_runnable_thread_count);

    num_shepherds = 0;
    total_threads = 0;
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

static inline void check_thread_attendance(int shepherd, uint64_t tid) {
    uint64_t thread_per_shepherd = total_threads / num_shepherds;
    int j, already_exists = 0;

    for (j = 0; j < thread_per_shepherd; j++) {
        if (thread_attendance[shepherd][j] == tid) {
            already_exists = 1;
            break;
        }
        if (thread_attendance[shepherd][j] == INT_MAX) {
            thread_attendance[shepherd][j] = tid;
            break;
        }
    }

    if (already_exists) return;
    if (j == thread_per_shepherd - 1) 
        are_all_threads_started[shepherd] = 1;
}

int shmem_internal_add_to_thread_queue(shmem_ctx_t *ctx, int reason, uint64_t cnt, uint64_t value) {
    if (!thread_scheduler_initialized) return -1;
    if (shmem_internal_get_thread_handle_fn == NULL || shmem_internal_getultinfo_fn == NULL) {
        fprintf(stderr, "Unable to get thread handle as such function is " 
                        "not registered\n");
        return -1;
    }

    if (q_policy == POLICY_NONE) { return -1; }

    uint64_t tid; 
    int shepherd; 
    shmem_internal_getultinfo_fn(&shepherd, &tid); 
    int tid_exists = 0;

    shmem_fiber *temp = pending_fiber_ll[shepherd], *prev_temp = NULL;
    while (temp != NULL) {
        if (temp->thread_user_id == tid) {
            temp->attached_ctx = ctx;
            temp->fiber_blocked_reason = reason;
            temp->fiber_blocked_cnt = cnt;
            temp->fiber_blocked_value = value;
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
        temp->fiber_blocked_cnt = cnt;
        temp->fiber_blocked_value = value;
        temp->is_waiting = 1;
        temp->is_runnable = 0;
        temp->next = NULL;

        append(shepherd, temp);

        if (!are_all_threads_started[shepherd]) check_thread_attendance(shepherd, tid);
        if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, tid, 0);
    } else {
        if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, tid, 2);
    }
    return 0;
}

void shmem_internal_remove_from_thread_queue(void) {
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
    if (!are_all_threads_started[shepherd]) check_thread_attendance(shepherd, tid);
    if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, tid, 1);
}

int shmem_internal_runnable_thread_exists(void) {
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
        return SCHEDULER_RET_CODE_QUEUE_EMPTY;
    }

    if (!are_all_threads_started[shepherd]) {
        return SCHEDULER_RET_CODE_ALL_THREADS_NOT_STARTED;
    }

    int ret_code = SCHEDULER_RET_CODE_NO_RUNNABLE_THREADS;
    shmem_fiber *queue = pending_fiber_ll[shepherd];

    if (current_runnable_thread_count[shepherd] > 1) {
        while (queue != NULL) {
            if (queue->thread_user_id != caller_tid && queue->is_runnable == 1) {
                next_runnable[shepherd] = queue;
                return queue->thread_user_id;
            }
            queue = queue->next;
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
            }
        }
        queue = queue->next;
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

    if (!are_all_threads_started[shepherd]) {
        return SCHEDULER_RET_CODE_ALL_THREADS_NOT_STARTED; 
    }

    int ret_code = SCHEDULER_RET_CODE_NO_RUNNABLE_THREADS;
    uint64_t ret_tid = 0;

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

    if (log_verbose && shmem_internal_my_pe == 0) display_ll(shepherd, ret_tid, 3);

    return ret_code;
}


