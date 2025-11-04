// sha256.cpp
#include "sha256.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// --- internal helpers ---
static inline uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
  static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
  };
  uint32_t W[64];
  uint32_t a,b,c,d,e,f,g,h;
  for (int i=0;i<16;i++) {
    W[i] = (uint32_t)block[i*4] << 24 |
           (uint32_t)block[i*4+1] << 16 |
           (uint32_t)block[i*4+2] << 8 |
           (uint32_t)block[i*4+3];
  }
  for (int i=16;i<64;i++) {
    uint32_t s0 = rotr(W[i-15],7) ^ rotr(W[i-15],18) ^ (W[i-15] >> 3);
    uint32_t s1 = rotr(W[i-2],17) ^ rotr(W[i-2],19) ^ (W[i-2] >> 10);
    W[i] = W[i-16] + s0 + W[i-7] + s1;
  }
  a = state[0]; b = state[1]; c = state[2]; d = state[3];
  e = state[4]; f = state[5]; g = state[6]; h = state[7];
  for (int i=0;i<64;i++) {
    uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + S1 + ch + K[i] + W[i];
    uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = S0 + maj;
    h = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_init(sha256_ctx *ctx) {
  ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
  ctx->bitcount = 0;
  memset(ctx->buffer, 0, 64);
}

void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len) {
  size_t idx = (ctx->bitcount >> 3) % 64;
  ctx->bitcount += (uint64_t)len << 3;
  for (size_t i=0;i<len;i++) {
    ctx->buffer[idx++] = data[i];
    if (idx == 64) {
      sha256_transform(ctx->state, ctx->buffer);
      idx = 0;
    }
  }
}

void sha256_final(sha256_ctx *ctx, uint8_t hash[SHA256_HASH_SIZE]) {
  size_t idx = (ctx->bitcount >> 3) % 64;
  ctx->buffer[idx++] = 0x80;
  if (idx > 56) {
    while (idx < 64) ctx->buffer[idx++] = 0;
    sha256_transform(ctx->state, ctx->buffer);
    idx = 0;
  }
  while (idx < 56) ctx->buffer[idx++] = 0;
  uint64_t bc = ctx->bitcount;
  for (int i=7;i>=0;i--) {
    ctx->buffer[idx++] = (uint8_t)(bc >> (i*8));
  }
  sha256_transform(ctx->state, ctx->buffer);
  for (int i=0;i<8;i++) {
    hash[i*4]   = (uint8_t)(ctx->state[i] >> 24);
    hash[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
    hash[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
    hash[i*4+3] = (uint8_t)(ctx->state[i]);
  }
}

/* HMAC-SHA256 implementation (uses sha256_* above) */
void hmac_sha256(const uint8_t *key, size_t keylen,
                 const uint8_t *msg, size_t msglen,
                 uint8_t out[SHA256_HASH_SIZE])
{
  uint8_t k_ipad[SHA256_BLOCK_SIZE];
  uint8_t k_opad[SHA256_BLOCK_SIZE];
  uint8_t tk[SHA256_HASH_SIZE];

  if (keylen > SHA256_BLOCK_SIZE) {
    sha256_ctx tctx;
    sha256_init(&tctx);
    sha256_update(&tctx, key, keylen);
    sha256_final(&tctx, tk);
    key = tk;
    keylen = SHA256_HASH_SIZE;
  }

  memset(k_ipad, 0, SHA256_BLOCK_SIZE);
  memset(k_opad, 0, SHA256_BLOCK_SIZE);
  memcpy(k_ipad, key, keylen);
  memcpy(k_opad, key, keylen);
  for (int i=0;i<SHA256_BLOCK_SIZE;i++) {
    k_ipad[i] ^= 0x36;
    k_opad[i] ^= 0x5c;
  }

  sha256_ctx ctx;
  uint8_t inner_hash[SHA256_HASH_SIZE];

  sha256_init(&ctx);
  sha256_update(&ctx, k_ipad, SHA256_BLOCK_SIZE);
  sha256_update(&ctx, msg, msglen);
  sha256_final(&ctx, inner_hash);

  sha256_init(&ctx);
  sha256_update(&ctx, k_opad, SHA256_BLOCK_SIZE);
  sha256_update(&ctx, inner_hash, SHA256_HASH_SIZE);
  sha256_final(&ctx, out);
}

