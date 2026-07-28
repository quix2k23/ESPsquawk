#ifndef MBEDTLS_CTR_DRBG_H
#define MBEDTLS_CTR_DRBG_H

#include "mbedtls/entropy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mbedtls_ctr_drbg_context {
    int dummy;
} mbedtls_ctr_drbg_context;

static inline void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context *ctx) { (void)ctx; }
static inline void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context *ctx) { (void)ctx; }
static inline int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context *ctx, 
                  int (*f_rng)(void *, unsigned char *, size_t), 
                  void *p_rng, const unsigned char *custom, size_t len) { 
    (void)ctx; (void)f_rng; (void)p_rng; (void)custom; (void)len; return 0; 
}
static inline int mbedtls_ctr_drbg_random(void *p_rng, unsigned char *output, size_t output_len) { 
    return mbedtls_entropy_func(p_rng, output, output_len); 
}

#ifdef __cplusplus
}
#endif

#endif