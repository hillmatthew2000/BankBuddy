#include "sha256.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

static uint32_t load_be32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static void store_be32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/* Processes one block whose 16 message words are already in w[0..15]; w is used as scratch. */
static void compress_words(uint32_t state[8], uint32_t w[64]) {
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 =
            h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void compress(uint32_t state[8], const unsigned char block[SHA256_BLOCK_SIZE]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = load_be32(block + 4 * i);
    compress_words(state, w);
}

void sha256_init(Sha256 *ctx) {
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(ctx->state, initial, sizeof initial);
    ctx->length = 0;
    ctx->used = 0;
}

void sha256_update(Sha256 *ctx, const void *data, size_t len) {
    const unsigned char *p = data;
    ctx->length += len;
    while (len > 0) {
        size_t take = SHA256_BLOCK_SIZE - ctx->used;
        if (take > len)
            take = len;
        memcpy(ctx->block + ctx->used, p, take);
        ctx->used += take;
        p += take;
        len -= take;
        if (ctx->used == SHA256_BLOCK_SIZE) {
            compress(ctx->state, ctx->block);
            ctx->used = 0;
        }
    }
}

void sha256_final(Sha256 *ctx, unsigned char digest[SHA256_DIGEST_SIZE]) {
    uint64_t bits = ctx->length * 8;
    ctx->block[ctx->used++] = 0x80;
    if (ctx->used > SHA256_BLOCK_SIZE - 8) {
        memset(ctx->block + ctx->used, 0, SHA256_BLOCK_SIZE - ctx->used);
        compress(ctx->state, ctx->block);
        ctx->used = 0;
    }
    memset(ctx->block + ctx->used, 0, SHA256_BLOCK_SIZE - 8 - ctx->used);
    store_be32(ctx->block + 56, (uint32_t)(bits >> 32));
    store_be32(ctx->block + 60, (uint32_t)bits);
    compress(ctx->state, ctx->block);
    for (int i = 0; i < 8; i++)
        store_be32(digest + 4 * i, ctx->state[i]);
    secure_zero(ctx, sizeof *ctx);
}

void sha256(const void *data, size_t len, unsigned char digest[SHA256_DIGEST_SIZE]) {
    Sha256 ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* An HMAC key schedule: hash states that have already absorbed the inner and outer pads. */
typedef struct HmacKey {
    Sha256 inner, outer;
} HmacKey;

static void hmac_key_init(HmacKey *hk, const void *key, size_t key_len) {
    unsigned char k[SHA256_BLOCK_SIZE] = {0};
    if (key_len > SHA256_BLOCK_SIZE)
        sha256(key, key_len, k);
    else if (key_len > 0)
        memcpy(k, key, key_len);

    unsigned char pad[SHA256_BLOCK_SIZE];
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++)
        pad[i] = (unsigned char)(k[i] ^ 0x36);
    sha256_init(&hk->inner);
    sha256_update(&hk->inner, pad, sizeof pad);
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; i++)
        pad[i] = (unsigned char)(k[i] ^ 0x5c);
    sha256_init(&hk->outer);
    sha256_update(&hk->outer, pad, sizeof pad);
    secure_zero(k, sizeof k);
    secure_zero(pad, sizeof pad);
}

/* MAC of the concatenation of up to two buffers (the second may be NULL). */
static void hmac_compute(const HmacKey *hk, const void *a, size_t a_len, const void *b,
                         size_t b_len, unsigned char mac[SHA256_DIGEST_SIZE]) {
    Sha256 ctx = hk->inner;
    sha256_update(&ctx, a, a_len);
    if (b)
        sha256_update(&ctx, b, b_len);
    unsigned char inner[SHA256_DIGEST_SIZE];
    sha256_final(&ctx, inner);
    ctx = hk->outer;
    sha256_update(&ctx, inner, sizeof inner);
    sha256_final(&ctx, mac);
    secure_zero(inner, sizeof inner);
}

void hmac_sha256(const void *key, size_t key_len, const void *data, size_t data_len,
                 unsigned char mac[SHA256_DIGEST_SIZE]) {
    HmacKey hk;
    hmac_key_init(&hk, key, key_len);
    hmac_compute(&hk, data, data_len, NULL, 0, mac);
    secure_zero(&hk, sizeof hk);
}

/*
 * HMAC of a 32-byte message given as words, in place. After the 64-byte key pad, such a
 * message plus its padding is exactly one block, so each hash is a single compression; this
 * is the PBKDF2 inner loop, where generic update/final bookkeeping would dominate the cost.
 */
static void hmac_digest_words(const HmacKey *hk, uint32_t u[8], uint32_t w[64]) {
    uint32_t s[8];
    const Sha256 *pass[2] = {&hk->inner, &hk->outer};
    for (int p = 0; p < 2; p++) {
        memcpy(s, pass[p]->state, sizeof s);
        memcpy(w, u, 8 * sizeof *w);
        w[8] = 0x80000000u;
        memset(w + 9, 0, 6 * sizeof *w);
        w[15] = (SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE) * 8; /* message length in bits */
        compress_words(s, w);
        memcpy(u, s, sizeof s);
    }
}

void pbkdf2_hmac_sha256(const void *password, size_t password_len, const void *salt,
                        size_t salt_len, uint32_t iterations, unsigned char *out, size_t out_len) {
    HmacKey hk;
    hmac_key_init(&hk, password, password_len);
    unsigned char first[SHA256_DIGEST_SIZE];
    uint32_t u[8], t[8], w[64];
    for (uint32_t block = 1; out_len > 0; block++) {
        unsigned char index[4];
        store_be32(index, block);
        hmac_compute(&hk, salt, salt_len, index, sizeof index, first);
        for (int i = 0; i < 8; i++)
            t[i] = u[i] = load_be32(first + 4 * i);
        for (uint32_t n = 1; n < iterations; n++) {
            hmac_digest_words(&hk, u, w);
            for (int i = 0; i < 8; i++)
                t[i] ^= u[i];
        }
        for (int i = 0; i < 8; i++)
            store_be32(first + 4 * i, t[i]);
        size_t take = out_len < sizeof first ? out_len : sizeof first;
        memcpy(out, first, take);
        out += take;
        out_len -= take;
    }
    secure_zero(&hk, sizeof hk);
    secure_zero(first, sizeof first);
    secure_zero(u, sizeof u);
    secure_zero(t, sizeof t);
    secure_zero(w, sizeof w);
}

int bytes_equal_ct(const void *a, const void *b, size_t len) {
    const volatile unsigned char *x = a;
    const volatile unsigned char *y = b;
    unsigned char diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= (unsigned char)(x[i] ^ y[i]);
    return diff == 0;
}

void secure_zero(void *p, size_t len) {
    volatile unsigned char *v = p;
    while (len--)
        *v++ = 0;
}
