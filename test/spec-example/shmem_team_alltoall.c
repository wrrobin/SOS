/*
 *  This test program is derived from an example program in the
 *  OpenSHMEM specification.
 */

#include <stdio.h>
#include <inttypes.h>
#include <shmem.h>

int main(void)
{
   shmem_init();
   int me = shmem_my_pe();
   int npes = shmem_n_pes();

   const int count = 2;
   int64_t* dest = (int64_t*) shmem_malloc(count * npes * sizeof(int64_t));
   int64_t* source = (int64_t*) shmem_malloc(count * npes * sizeof(int64_t));

   /* assign source values */
   for (int pe = 0; pe < npes; pe++) {
      for (int i = 0; i < count; i++) {
         source[(pe * count) + i] = me + pe;
         dest[(pe * count) + i] = 9999;
      }
   }
   /* wait for all PEs to initialize source/dest */
   shmem_team_sync(SHMEM_TEAM_WORLD);

   /* alltoall on all PES */
   shmem_int64_alltoall(SHMEM_TEAM_WORLD, dest, source, count);

   /* verify results */
   for (int pe = 0; pe < npes; pe++) {
      for (int i = 0; i < count; i++) {
         if (dest[(pe * count) + i] != pe + me) {
            printf("[%d] ERROR: dest[%d]=%" PRId64 ", should be %d\n",
               me, (pe * count) + i, dest[(pe * count) + i], pe + me);
           }
       }
   }

   shmem_free(dest);
   shmem_free(source);
   shmem_finalize();
   return 0;
}
