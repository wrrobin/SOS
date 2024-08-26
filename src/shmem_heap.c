/* -*- C -*-
 *
 * Copyright (c) 2024 Intel Corporation. All rights reserved.
 * This software is available to you under the BSD license.
 *
 * This file is part of the Sandia OpenSHMEM software package. For license
 * information, see the LICENSE file in the top level directory of the
 * distribution.
 *
 */

#include "config.h"

#include "shmem_heap.h"
#include "shmem_internal.h"
#include "shmem_collectives.h"

shmem_internal_heap_t shmem_main_heap;
shmem_internal_heap_t *shmem_external_heaps;
int current_external_heaps;

int shmem_internal_heap_init(void) {

    int ret = 0;
    /* Set up main heap */
    shmem_main_heap.device_idx = -1;
    shmem_main_heap.device_type = -1;
    shmem_main_heap.base = (void *) shmem_internal_heap_base;
    shmem_main_heap.size = (size_t) shmem_internal_heap_length;

    /* Allocate external heaps */
    shmem_external_heaps = (shmem_internal_heap_t *) malloc(shmem_internal_params.MAX_EXTERNAL_HEAP_COUNT * 
                                                            sizeof(shmem_internal_heap_t));
    if (shmem_external_heaps == NULL) {
        RAISE_ERROR_MSG("External heap object allocation failed\n");
        goto fn_fail;
    }
    current_external_heaps = 0;

fn_exit:
    return ret;
fn_fail:
    ret = -1;
    goto fn_exit;
}

void shmem_internal_heap_fini(void) {
    shmem_main_heap.size = 0;
    free(shmem_external_heaps);
}

int shmem_internal_heap_create(void *base, size_t size, int device_type, int device_index, shmem_internal_heap_t **heap) {

    /* Check for size */
    shmem_internal_assert(size > 0);
    /* Check for number of heap limit */
    shmem_internal_assert(current_external_heaps < shmem_internal_params.MAX_EXTERNAL_HEAP_COUNT);

    /* Check for device type */
    shmem_internal_assert(device_type == SHMEMX_EXTERNAL_HEAP_ZE ||
                          device_type == SHMEMX_EXTERNAL_HEAP_CUDA);
    /* Check for device index */
#ifndef USE_FI_HMEM
    shmem_internal_assert(device_index == -1);
#else
    shmem_internal_assert(device_index != -1);
#endif

    SHMEM_MUTEX_LOCK(shmem_internal_mutex_alloc);

    shmem_external_heaps[current_external_heaps].base = base;
    shmem_external_heaps[current_external_heaps].size = size;
    shmem_external_heaps[current_external_heaps].device_type = device_type;
    shmem_external_heaps[current_external_heaps].device_idx = device_index;
    shmem_external_heaps[current_external_heaps].heap_backend =
        create_mspace_with_base(
            shmem_external_heaps[current_external_heaps].base, 
            shmem_external_heaps[current_external_heaps].size, 
            0);

    *heap = &shmem_external_heaps[current_external_heaps];
    current_external_heaps++;

    SHMEM_MUTEX_UNLOCK(shmem_internal_mutex_alloc);

    shmem_external_heap_pre_initialized = 1;

    return 0;
}

void *shmem_internal_heap_malloc(shmem_internal_heap_t *heap, size_t size) {
    void *ret = NULL;

    SHMEM_MUTEX_LOCK(shmem_internal_mutex_alloc);
    ret = mspace_memalign(heap->heap_backend, (size_t) 64, size);
    SHMEM_MUTEX_UNLOCK(shmem_internal_mutex_alloc);

    shmem_internal_barrier_all();

    return ret;
}

void *shmem_internal_heap_calloc(shmem_internal_heap_t *heap, size_t count, size_t size) {
    void *ret = NULL;

    SHMEM_MUTEX_LOCK(shmem_internal_mutex_alloc);
    ret = mspace_memalign(heap->heap_backend, (size_t) 64, size * count);
    // TODO assign the allocate space to 0
    SHMEM_MUTEX_UNLOCK(shmem_internal_mutex_alloc);

    shmem_internal_barrier_all();

    return ret;
}

void *shmem_internal_heap_align(shmem_internal_heap_t *heap, size_t alignment, size_t size) {
    void *ret = NULL;

    SHMEM_MUTEX_LOCK(shmem_internal_mutex_alloc);
    ret = mspace_memalign(heap->heap_backend, alignment, size);
    SHMEM_MUTEX_UNLOCK(shmem_internal_mutex_alloc);

    shmem_internal_barrier_all();

    return ret;
}

void shmem_internal_heap_free(shmem_internal_heap_t *heap, void *ptr) {
    shmem_internal_barrier_all();

    SHMEM_MUTEX_LOCK(shmem_internal_mutex_alloc);
    ret = mspace_free(heap->heap_backend, ptr);
    SHMEM_MUTEX_UNLOCK(shmem_internal_mutex_alloc);
}
