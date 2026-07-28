#ifndef RID_SECURITY_H
#define RID_SECURITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_remote_id.h"

/*
 * Verifies that `sig_b64` (a base64-encoded Ed25519/EC signature, as sent in
 * the X-Signature header) is a valid signature over `body`, produced by one
 * of the public keys stored in cfg->public_keys[]. Returns true iff at
 * least one configured key verifies the signature.
 *
 * `body` must be a NUL-terminated string. This is used both for whole JSON
 * request bodies (config writes, commands) and for short fixed strings
 * (e.g. an expected-SHA256 hex string, for OTA uploads).
 */
bool rid_security_verify_signature(const char *body, const char *sig_b64, const rid_config_t *cfg);

/*
 * Minimal base64 decoder. Ignores '=' padding and any characters not in the
 * base64 alphabet. Returns the decoded length, or -1 if `out_size` would be
 * exceeded.
 */
int rid_security_b64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_size);

#endif /* RID_SECURITY_H */
