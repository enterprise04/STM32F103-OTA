#ifndef __SHA256_H
#define __SHA256_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t hash[32]);

/* 一次性计算:对 data[len] 求 SHA-256,结果写入 hash[32] */
void sha256(const uint8_t *data, size_t len, uint8_t hash[32]);

#endif /* __SHA256_H */
