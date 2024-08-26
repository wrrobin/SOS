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

#ifndef SHMEM_HEAP_H
#define SHMEM_HEAP_H

#include <sys/types.h>

typedef void *mspace;
/* mspace routines */
mspace create_mspace_with_base(void *, size_t, int);
void *mspace_memalign(mspace, size_t, size_t);
void mspace_free(mspace, void *);

struct shmem_internal_heap_t {
    int device_idx;
    int device_type;
    void *base;
    size_t size;
    mspace heap_backend;
};
typedef struct shmem_internal_heap_t shmem_internal_heap_t;

extern shmem_internal_heap_t shmem_main_heap;
extern shmem_internal_heap_t *shmem_external_heaps;
extern int current_external_heaps;

/* Heap admin routines */
int shmem_internal_heap_init(void);
void shmem_internal_heap_fini(void);

/* Heap setup routines */
int shmem_internal_heap_create(void *, size_t, int, int, shmem_internal_heap_t **);

/* Heap usage routines */
void *shmem_internal_heap_malloc(shmem_internal_heap_t *, size_t);
void *shmem_internal_heap_calloc(shmem_internal_heap_t *, size_t, size_t);
void *shmem_internal_heap_align(shmem_internal_heap_t *, size_t, size_t);
void shmem_internal_heap_free(shmem_internal_heap_t *, void *);

#endif /* SHMEM_HEAP_H */
