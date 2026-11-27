#include "hmac_sha256.h"

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static inline uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

struct sha256_ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buf_len;
};

static void sha256_transform(struct sha256_ctx *ctx, const uint8_t block[64])
{
    uint32_t w[64];

    for (size_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[4 * i] << 24) |
               ((uint32_t)block[4 * i + 1] << 16) |
               ((uint32_t)block[4 * i + 2] << 8) |
               ((uint32_t)block[4 * i + 3]);
    }
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (size_t i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(struct sha256_ctx *ctx)
{
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bitlen = 0;
    ctx->buf_len = 0;
}

static void sha256_update(struct sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->buf_len++] = data[i];
        if (ctx->buf_len == 64) {
            sha256_transform(ctx, ctx->buf);
            ctx->bitlen += 512;
            ctx->buf_len = 0;
        }
    }
}

static void sha256_final(struct sha256_ctx *ctx, uint8_t out[32])
{
    uint64_t total_bits = ctx->bitlen + (uint64_t)ctx->buf_len * 8u;

    ctx->buf[ctx->buf_len++] = 0x80u;
    if (ctx->buf_len > 56) {
        while (ctx->buf_len < 64)
            ctx->buf[ctx->buf_len++] = 0;
        sha256_transform(ctx, ctx->buf);
        ctx->buf_len = 0;
    }
    while (ctx->buf_len < 56)
        ctx->buf[ctx->buf_len++] = 0;

    for (int i = 0; i < 8; i++)
        ctx->buf[56 + i] = (uint8_t)(total_bits >> (56 - 8 * i));

    sha256_transform(ctx, ctx->buf);

    for (size_t i = 0; i < 8; i++) {
        out[4 * i]     = (uint8_t)(ctx->state[i] >> 24);
        out[4 * i + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[4 * i + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[4 * i + 3] = (uint8_t)(ctx->state[i]);
    }
}

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *msg, size_t msg_len,
                 uint8_t out32[32])
{
    uint8_t k_pad[64];
    uint8_t inner_hash[32];
    struct sha256_ctx ctx;

    for (size_t i = 0; i < 64; i++)
        k_pad[i] = 0;

    if (key_len > 64) {
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, k_pad);
    } else {
        for (size_t i = 0; i < key_len; i++)
            k_pad[i] = key[i];
    }

    for (size_t i = 0; i < 64; i++)
        k_pad[i] ^= 0x36u;

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, msg, msg_len);
    sha256_final(&ctx, inner_hash);

    for (size_t i = 0; i < 64; i++)
        k_pad[i] ^= (0x36u ^ 0x5cu);

    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, out32);
}

int ct_memcmp32(const uint8_t a[32], const uint8_t b[32])
{
    uint8_t diff = 0;
    for (size_t i = 0; i < 32; i++)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return diff != 0;
}
