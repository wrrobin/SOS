/*
 *  Copyright (c) 2019 Intel Corporation. All rights reserved.
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
#include <shmem.h>

int main(void)
{
    int i, me, npes;
    int ret = 0, errors = 0;

    shmem_init();

    me = shmem_my_pe();
    npes = shmem_n_pes();

    if (me == 0)
        printf("Reuse teams test\n");

    shmem_team_t old_team, new_team;
    ret = shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 1, npes, NULL, 0, &old_team);
    if (ret) ++errors;

    /* A total of npes-1 iterations are performed, where the active set in iteration i
     * includes PEs i..npes-1.  The size of the team decreases by 1 each iteration.  */
    for (i = 1; i < npes; i++) {

        if (me == i) {
            printf("%3d: creating new team (start, stride, size): %3d, %3d, %3d\n", me,
                shmem_team_translate_pe(old_team, 1, SHMEM_TEAM_WORLD), 1, shmem_team_n_pes(old_team)-1);
        }

        ret = shmem_team_split_strided(old_team, 1, 1, shmem_team_n_pes(old_team)-1, NULL, 0, &new_team);
        if (old_team != SHMEM_TEAM_INVALID && ret) ++errors;

        shmem_team_destroy(old_team);
        old_team = new_team;
    }

    shmem_team_destroy(old_team);
    shmem_finalize();

    return errors != 0;
}
