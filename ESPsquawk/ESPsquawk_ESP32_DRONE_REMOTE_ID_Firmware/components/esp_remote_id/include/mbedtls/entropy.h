#ifndef MBEDTLS_ENTROPY_H
#define MBEDTLS_ENTROPY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "esp_random.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mbedtls_entropy_context {
    int dummy;
} mbedtls_entropy_context;

static inline void mbedtls_entropy_init(mbedtls_entropy_context *ctx) { (void)ctx; }
static inline void mbedtls_entropy_free(mbedtls_entropy_context *ctx) { (void)ctx; }

static inline int mbedtls_entropy_func(void *data, unsigned char *output, size_t len) {
    (void)data;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = esp_random();
        size_t copy = (len - i >= 4) ? 4 : (len - i);
        memcpy(output + i, &r, copy);
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif