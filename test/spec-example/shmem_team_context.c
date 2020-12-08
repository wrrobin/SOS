/*
 *  This test program is derived from an example program in the
 *  OpenSHMEM specification.
 */

#include <shmem.h>
#include <stdio.h>


int main(void)
{
  static int            sum = 0, val_2, val_3;
  shmem_team_t         team_2, team_3;
  shmem_ctx_t           ctx_2, ctx_3;
  shmem_team_config_t  conf;

  shmem_init();

  int npes = shmem_n_pes();
  int mype = shmem_my_pe();
  conf.num_contexts = 1;
  long cmask = SHMEM_TEAM_NUM_CONTEXTS;

  /* Create team_2 with PEs numbered 0, 2, 4, ... */
  int ret = shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 2, (npes + 1) / 2, &conf, cmask, &team_2);

  if (ret != 0) {
      printf("%d: Error creating team team_2 (%d)\n", mype, ret);
      shmem_global_exit(ret);
  }

  /* Create team_3 with PEs numbered 0, 3, 6, ... */
  ret = shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 3, (npes + 2) / 3, &conf, cmask, &team_3);

  if (ret != 0) {
      printf("%d: Error creating team team_3 (%d)\n", mype, ret);
      shmem_global_exit(ret);
  }

  /* Create a context on team_2. */
  ret = shmem_team_create_ctx(team_2, 0, &ctx_2);

  if (ret != 0 && team_2 != SHMEM_TEAM_INVALID) {
      printf("%d: Error creating context ctx_2 (%d)\n", mype, ret);
      shmem_global_exit(ret);
  }

  /* Create a context on team_3. */
  ret = shmem_team_create_ctx(team_3, 0, &ctx_3);

  if (ret != 0 && team_3 != SHMEM_TEAM_INVALID) {
      printf("%d: Error creating context ctx_3 (%d)\n", mype, ret);
      shmem_global_exit(ret);
  }

  /* Within each team, put my PE number to my neighbor in a ring-based manner. */
  if (ctx_2 != SHMEM_CTX_INVALID) {
      int pe = shmem_team_my_pe(team_2);
      shmem_ctx_int_put(ctx_2, &val_2, &pe, 1, (pe + 1) % shmem_team_n_pes(team_2));
  }

  if (ctx_3 != SHMEM_CTX_INVALID) {
      int pe = shmem_team_my_pe(team_3);
      shmem_ctx_int_put(ctx_3, &val_3, &pe, 1, (pe + 1) % shmem_team_n_pes(team_3));
  }

  /* Quiet both contexts and synchronize all PEs to complete the data transfers. */
  shmem_ctx_quiet(ctx_2);
  shmem_ctx_quiet(ctx_3);
  shmem_team_sync(SHMEM_TEAM_WORLD);

  /* Sum the values among PEs that are in both team_2 and team_3 on PE 0 with ctx_2. */
  if (team_3 != SHMEM_TEAM_INVALID && team_2 != SHMEM_TEAM_INVALID)
      shmem_ctx_int_atomic_add(ctx_2, &sum, val_2 + val_3, 0);

  /* Quiet the context and synchronize PEs to complete the operation. */
  shmem_ctx_quiet(ctx_2);
  shmem_team_sync(SHMEM_TEAM_WORLD);

  /* Validate the result. */
  if (mype == 0) {
      int vsum = 0;
      for (int i = 0; i < npes; i ++) {
          if (i % 2 == 0 && i % 3 == 0) {
              vsum += ((i - 2) < 0) ? shmem_team_n_pes(team_2) - 1 :
                  shmem_team_translate_pe(SHMEM_TEAM_WORLD, i - 2, team_2);
              vsum += ((i - 3) < 0) ? shmem_team_n_pes(team_3) - 1 :
                  shmem_team_translate_pe(SHMEM_TEAM_WORLD, i - 3, team_3);
          }
      }
      if (sum != vsum) {
          fprintf(stderr, "Unexpected result, npes = %d, vsum = %d, sum = %d\n", shmem_n_pes(), vsum, sum);
          shmem_global_exit(1);
      }
  }

  /* Destroy contexts before teams. */
  shmem_ctx_destroy(ctx_2);
  shmem_team_destroy(team_2);

  shmem_ctx_destroy(ctx_3);
  shmem_team_destroy(team_3);

  shmem_finalize();
  return 0;
}
