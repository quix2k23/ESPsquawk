#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "mbedtls/pk.h"
#include "psa/crypto.h"
#include "rid_security.h"

static const char b64_tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int rid_security_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_size)
{
    int len = 0;
    uint8_t buf[4];
    int buf_i = 0;
    for (size_t i = 0; i < in_len && in[i] != '='; i++) {
        char c = in[i];
        const char *p = strchr(b64_tab, c);
        if (!p) continue;
        buf[buf_i++] = (uint8_t)(p - b64_tab);
        if (buf_i == 4) {
            if (len >= (int)out_size) return -1;
            out[len++] = (buf[0] << 2) | (buf[1] >> 4);
            if (len >= (int)out_size) return -1;
            out[len++] = (buf[1] << 4) | (buf[2] >> 2);
            if (len >= (int)out_size) return -1;
            out[len++] = (buf[2] << 6) | buf[3];
            buf_i = 0;
        }
    }
    if (buf_i >= 2) {
        if (len >= (int)out_size) return -1;
        out[len++] = (buf[0] << 2) | (buf[1] >> 4);
    }
    if (buf_i >= 3) {
        if (len >= (int)out_size) return -1;
        out[len++] = (buf[1] << 4) | (buf[2] >> 2);
    }
    return len;
}

bool rid_security_verify_signature(const char *body, const char *sig_b64, const rid_config_t *cfg)
{
    if (!body || !sig_b64 || !*sig_b64 || !cfg) return false;

    size_t b64_len = strlen(sig_b64);
    size_t sig_max = (b64_len * 3) / 4 + 4;
    uint8_t *sig = (uint8_t *)malloc(sig_max);
    if (!sig) return false;

    int sig_len = rid_security_b64_decode(sig_b64, b64_len, sig, sig_max);
    if (sig_len <= 0) { free(sig); return false; }

    uint8_t hash[32];
    size_t hash_len;
    if (psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)body, strlen(body),
                         hash, sizeof(hash), &hash_len) != PSA_SUCCESS) {
        free(sig);
        return false;
    }

    bool verified = false;
    for (int i = 0; i < ESP_RID_NUM_KEYS; i++) {
        const char *key_str = cfg->public_keys[i];
        if (!key_str || !*key_str) continue;

        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);

        int ret;
        size_t key_len = strlen(key_str);

        /* Try as PEM or DER first */
        ret = mbedtls_pk_parse_public_key(&pk, (const uint8_t *)key_str, key_len);
        if (ret != 0) {
            /* Try after stripping PUBLIC_KEYV1: prefix */
            const char *prefix = "PUBLIC_KEYV1:";
            size_t plen = strlen(prefix);
            if (key_len > plen && strncasecmp(key_str, prefix, plen) == 0) {
                const char *payload = key_str + plen;
                size_t payload_len = key_len - plen;
                uint8_t *key_bin = (uint8_t *)malloc(payload_len);
                if (key_bin) {
                    int key_bin_len = rid_security_b64_decode(payload, payload_len, key_bin, payload_len);
                    if (key_bin_len > 0) {
                        mbedtls_pk_free(&pk);
                        mbedtls_pk_init(&pk);
                        ret = mbedtls_pk_parse_public_key(&pk, key_bin, key_bin_len);
                    }
                    free(key_bin);
                }
            }
        }

        if (ret != 0) {
            mbedtls_pk_free(&pk);
            continue;
        }

        ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, hash_len, sig, sig_len);
        mbedtls_pk_free(&pk);

        if (ret == 0) {
            verified = true;
            break;
        }
    }

    free(sig);
    return verified;
}
