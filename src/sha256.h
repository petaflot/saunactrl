#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_HASH_SIZE 32
#define SHA256_BLOCK_SIZE 64

typedef struct {
  uint32_t state[8];
  uint64_t bitcount;
  uint8_t buffer[64];
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t hash[SHA256_HASH_SIZE]);

/**
 * HMAC-SHA256
 * key, keylen: secret key bytes
 * msg, msglen: message bytes
 * out: 32 bytes output
 */
void hmac_sha256(const uint8_t *key, size_t keylen,
                 const uint8_t *msg, size_t msglen,
                 uint8_t out[SHA256_HASH_SIZE]);

#ifdef __cplusplus
}
#endif

#endif // SHA256_H

