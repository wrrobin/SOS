/*
 * OpenSHMEM shmem_team_translate example to verify the team formed by even
 * ranked PEs from SHMEM_TEAM_WORLD using the team created from
 * shmem_team_split_stride operation
 */

#include <shmem.h>
#include <stdio.h>

int main(void)
{
    int                  my_pe, npes, errors = 0;
    int                  t_pe_2, t_pe_3, t_pe_2_to_3, t_pe_3_to_2;
    shmem_team_t        team_2s;
    shmem_team_t        team_3s;
    shmem_team_config_t *config;

    shmem_init();
    config = NULL;
    my_pe  = shmem_my_pe();
    npes   = shmem_n_pes();

    shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 2, ((npes-1)/2)+1, config, 0,
                             &team_2s);
    shmem_team_split_strided(SHMEM_TEAM_WORLD, 0, 3, ((npes-1)/3)+1, config, 0,
                             &team_3s);

    t_pe_3 = shmem_team_my_pe(team_3s);
    t_pe_2 = shmem_team_my_pe(team_2s);
    t_pe_3_to_2 = shmem_team_translate_pe(team_3s, t_pe_3, team_2s);
    t_pe_2_to_3 = shmem_team_translate_pe(team_2s, t_pe_2, team_3s);

    if (my_pe % 2 == 0 && my_pe % 3 == 0) {
        if (t_pe_2 == -1 || t_pe_3 == -1 || t_pe_2_to_3 == -1 || t_pe_3_to_2 == -1) {
            printf("ERROR: PE %d, t_pe_2=%d, t_pe_3=%d, t_pe_3_to_2=%d, t_pe_2_to_3=%d\n",
                   my_pe, t_pe_2, t_pe_3, t_pe_3_to_2, t_pe_2_to_3);
            ++errors;
        }
    } else if (my_pe % 2 == 0) {
        if (t_pe_2 == -1 || t_pe_3 != -1 || t_pe_2_to_3 != -1 || t_pe_3_to_2 != -1) {
            printf("ERROR: PE %d, t_pe_2=%d, t_pe_3=%d, t_pe_3_to_2=%d, t_pe_2_to_3=%d\n",
                   my_pe, t_pe_2, t_pe_3, t_pe_3_to_2, t_pe_2_to_3);
            ++errors;
        }
    } else if (my_pe % 3 == 0){
        if (t_pe_2 != -1 || t_pe_3 == -1 || t_pe_2_to_3 != -1 || t_pe_3_to_2 != -1) {
            printf("ERROR: PE %d, t_pe_2=%d, t_pe_3=%d, t_pe_3_to_2=%d, t_pe_2_to_3=%d\n",
                   my_pe, t_pe_2, t_pe_3, t_pe_3_to_2, t_pe_2_to_3);
            ++errors;
        }
    } else {
        if (t_pe_2 != -1 || t_pe_3 != -1 || t_pe_2_to_3 != -1 || t_pe_3_to_2 != -1) {
            printf("ERROR: PE %d, t_pe_2=%d, t_pe_3=%d, t_pe_3_to_2=%d, t_pe_2_to_3=%d\n",
                   my_pe, t_pe_2, t_pe_3, t_pe_3_to_2, t_pe_2_to_3);
            ++errors;
        }
    }

    shmem_finalize();
    return errors != 0;
}
