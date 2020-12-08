/* -*- C -*-
 *
 * Copyright 2011 Sandia Corporation. Under the terms of Contract
 * DE-AC04-94AL85000 with Sandia Corporation, the U.S.  Government
 * retains certain rights in this software.
 *
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
 * This software is available to you under the BSD license.
 *
 * This file is part of the Sandia OpenSHMEM software package. For license
 * information, see the LICENSE file in the top level directory of the
 * distribution.
 *
 */

#include "config.h"

#define SHMEM_INTERNAL_INCLUDE
#include "shmem.h"
#include "shmem_internal.h"
#include "transport.h"
#include "shmem_synchronization.h"
#include "shmem_team.h"

#ifdef ENABLE_PROFILING
#include "pshmem.h"

#pragma weak shmem_ctx_create = pshmem_ctx_create
#define shmem_ctx_create pshmem_ctx_create

#pragma weak shmem_ctx_destroy = pshmem_ctx_destroy
#define shmem_ctx_destroy pshmem_ctx_destroy

#pragma weak shmemx_register_gettid = pshmemx_register_gettid
#define shmemx_register_gettid pshmemx_register_gettid


#endif /* ENABLE_PROFILING */

SHMEM_FUNCTION_ATTRIBUTES int
shmem_ctx_create(long options, shmem_ctx_t *ctx)
{
    SHMEM_ERR_CHECK_INITIALIZED();

    int ret = shmem_transport_ctx_create(&shmem_internal_team_world, options, (shmem_transport_ctx_t **) ctx);

    if (0 != ret) *ctx = SHMEM_CTX_INVALID;

    return ret;
}


SHMEM_FUNCTION_ATTRIBUTES void
shmem_ctx_destroy(shmem_ctx_t ctx)
{
    SHMEM_ERR_CHECK_INITIALIZED();

    if (ctx == SHMEM_CTX_INVALID)
        return;

    if (ctx == SHMEM_CTX_DEFAULT) {
        fprintf(stderr, "ERROR: %s(): SHMEM_CTX_DEFAULT cannot be destroyed\n",
                __func__);
        shmem_runtime_abort(100, PACKAGE_NAME " exited in error");
    }

    shmem_internal_quiet(ctx);
    shmem_transport_ctx_destroy((shmem_transport_ctx_t *) ctx);

    return;
}

void SHMEM_FUNCTION_ATTRIBUTES
shmemx_register_gettid(uint64_t (*gettid_fn)(void))
{
    shmem_internal_register_gettid(gettid_fn);
    return;
}

void SHMEM_FUNCTION_ATTRIBUTES
shmemx_register_yield(void (*yield_fn)(int))
{
    shmem_internal_register_yield(yield_fn);
    return;
}

void SHMEM_FUNCTION_ATTRIBUTES
shmemx_register_getultinfo(void (*getultinfo_fn)(int *, uint64_t *))
{
    shmem_internal_register_getultinfo(getultinfo_fn);
    return;
}

void SHMEM_FUNCTION_ATTRIBUTES
shmemx_register_get_thread_handle(void* (*get_thread_handle_fn)(void))
{
    shmem_internal_register_get_thread_handle(get_thread_handle_fn);
    return;
}

int SHMEM_FUNCTION_ATTRIBUTES
shmemx_get_next_thread(void **next_thread)
{
    int ret = shmem_internal_get_next_thread(next_thread);
    return ret;
}

void SHMEM_FUNCTION_ATTRIBUTES
shmemx_thread_scheduler_init(uint64_t num_hw_threads, uint64_t num_ul_threads, int *priority_list)
{
    shmem_internal_thread_scheduler_init(num_hw_threads, num_ul_threads, priority_list);
    return;
}

void SHMEM_FUNCTION_ATTRIBUTES
shmemx_thread_scheduler_finalize(void)
{
    shmem_internal_thread_scheduler_finalize();
    return;
}

void SHMEM_FUNCTION_ATTRIBUTES
shmemx_thread_register(void)
{
    shmem_internal_thread_scheduler_register();
    return;
}

void SHMEM_FUNCTION_ATTRIBUTES
shmemx_thread_unregister(void)
{
    shmem_internal_remove_from_thread_queue();
    return;
}

