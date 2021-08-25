#ifndef SHMEMX_DEF_H
#define SHMEMX_DEF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAVE_SHMEMX_WTIME

/* Counting puts */
typedef char * shmemx_ct_t;

/* Counter */
typedef struct {
    uint64_t pending_put;
    uint64_t pending_get;
    uint64_t completed_put;
    uint64_t completed_get;
    uint64_t target;
} shmemx_pcntr_t;


#ifdef ENABLE_TOOLS
typedef enum {
    SHMEMX_T_VAR_TYPE_C,
    SHMEMX_T_VAR_TYPE_P
} shmemx_t_var_t;

typedef enum {
    SHMEMX_T_VAR_CAT_RMA,
    SHMEMX_T_VAR_CAT_AMO,
    SHMEMX_T_VAR_CAT_COLL
} shmemx_t_var_cat;

typedef enum {
    SHMEMX_T_VAR_SUBCAT_PUT,
    SHMEMX_T_VAR_SUBCAT_GET,
    SHMEMX_T_VAR_SUBCAT_AND,
    SHMEMX_T_VAR_SUBCAT_OR
} shmemx_t_var_subcat;

typedef enum {
    SHMEMX_T_VAR_BIND_CTX,
    SHMEMX_T_VAR_BIND_TEAM,
    SHMEMX_T_VAR_BIND_PE
} shmemx_t_var_bind;

typedef enum {
    SHMEMX_T_VAR_OP_ISSUED,
    SHMEMX_T_VAR_OP_PENDING,
    SHMEMX_T_VAR_OP_COMPLETE
} shmemx_t_var_op;

typedef enum {
    SHMEMX_T_VAR_CLASS_COUNTER,
    SHMEMX_T_VAR_CLASS_AGGREGATE,
    SHMEMX_T_VAR_CLASS_PERCENT
} shmemx_t_var_class;

#endif /* SHMEM_T */
#ifdef __cplusplus
}
#endif

#endif /* SHMEMX_DEF_H */
