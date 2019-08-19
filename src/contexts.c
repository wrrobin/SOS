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

uint64_t (*shmem_internal_gettid_fn)(void) = NULL;
void (*shmem_internal_yield_fn)(int) = NULL;
void* (*shmem_internal_get_thread_handle_fn)(void) = NULL;

void shmem_internal_register_gettid(uint64_t (*gettid_fn)(void))
{
    shmem_internal_gettid_fn = gettid_fn;
}

void shmem_internal_register_yield(void (*yield_fn)(int)) 
{
    shmem_internal_yield_fn = yield_fn;
}

void shmem_internal_register_get_thread_handle(void* (*get_thread_handle_fn)(void))
{
    shmem_internal_get_thread_handle_fn = get_thread_handle_fn;
}
