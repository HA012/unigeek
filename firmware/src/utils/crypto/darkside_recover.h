#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int darkside_recover_key(uint32_t uid, uint32_t nt, uint64_t par, uint64_t ks1,
                         uint32_t nr, uint32_t ar, uint8_t outKey[6]);
#ifdef __cplusplus
}
#endif
