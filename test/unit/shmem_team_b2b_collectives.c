/*  Copyright (c) 2019 Intel Corporation. All rights reserved.
 *  This software is available to you under the BSD license below:
 *
 *      Redistribution and use in source and binary forms, with or
 *      without modification, are permitted provided that the following
 *      conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <shmem.h>

#define NITERS 100
#define NELEMS 10

#define TEST_B2B_COLLECTIVE(NAME, ROUTINE, ...)                                 \
    do {                                                                        \
        if (me == 0) printf("%s... ", NAME);                                    \
        int i;                                                                  \
        for (i = 0; i < NITERS; i++) {                                          \
            errors += ROUTINE(__VA_ARGS__);                                     \
        }                                                                       \
        error_check(&errors, &total_errors, NAME, me);                          \
    } while (0)

static void error_check(int *errors, int *total_errors, char *routine, int me) {
    if (*errors == 0) {
        if (me == 0) printf("passed.\n");
    } else {
        printf("%s error on PE %d\n", routine, me);
        *total_errors += *errors;
        *errors = 0;
    }
    return;
}


int main(void)
{
    int errors = 0, total_errors = 0;
    shmem_init();
    int me = shmem_my_pe();

    long *dest = shmem_malloc(NELEMS * sizeof(long));
    long *src = shmem_malloc(NELEMS * sizeof(long));

    size_t i;
    for (i = 0; i < NELEMS; i++) {
        src[i] = me;
    }

    TEST_B2B_COLLECTIVE("broadcast", shmem_long_broadcast, SHMEM_TEAM_WORLD, dest, src, NELEMS, 0);
    TEST_B2B_COLLECTIVE("reduce", shmem_long_sum_reduce, SHMEM_TEAM_WORLD, dest, src, NELEMS);
    TEST_B2B_COLLECTIVE("collect", shmem_long_collect, SHMEM_TEAM_WORLD, dest, src, NELEMS);
    TEST_B2B_COLLECTIVE("fcollect", shmem_long_fcollect, SHMEM_TEAM_WORLD, dest, src, NELEMS);
    TEST_B2B_COLLECTIVE("alltoall", shmem_long_alltoall, SHMEM_TEAM_WORLD, dest, src, NELEMS);
    TEST_B2B_COLLECTIVE("alltoalls", shmem_long_alltoalls, SHMEM_TEAM_WORLD, dest, src, 1, 1, NELEMS);

    shmem_finalize();
    return total_errors;
}
