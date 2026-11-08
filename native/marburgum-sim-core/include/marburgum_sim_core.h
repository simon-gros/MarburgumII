#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Unreal runtime does not yet link this experimental native ABI. */
uint32_t marburgum_sim_core_abi_version(void);
uint64_t marburgum_deterministic_seed(
    uint64_t world_seed,
    uint32_t day_index,
    uint32_t district_id
);

#ifdef __cplusplus
}
#endif
