#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

/*
 * SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104) and PBKDF2-HMAC-SHA256 (RFC 8018).
 * A small self-contained implementation so the app needs no crypto library; it is checked
 * against the published test vectors in tests/test_sha256.c.
 */

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE  64

typedef struct Sha256 {
    uint32_t state[8];
    uint64_t length; /* bytes hashed so far */
    unsigned char block[SHA256_BLOCK_SIZE];
    size_t used; /* bytes waiting in `block` */
} Sha256;

void sha256_init(Sha256 *ctx);
void sha256_update(Sha256 *ctx, const void *data, size_t len);
void sha256_final(Sha256 *ctx, unsigned char digest[SHA256_DIGEST_SIZE]);
void sha256(const void *data, size_t len, unsigned char digest[SHA256_DIGEST_SIZE]);

void hmac_sha256(const void *key, size_t key_len, const void *data, size_t data_len,
                 unsigned char mac[SHA256_DIGEST_SIZE]);

/* Derives `out_len` bytes from a password and salt with `iterations` rounds (at least 1). */
void pbkdf2_hmac_sha256(const void *password, size_t password_len, const void *salt,
                        size_t salt_len, uint32_t iterations, unsigned char *out, size_t out_len);

/* Compares two buffers in time that depends only on `len`, not on where they differ. */
int bytes_equal_ct(const void *a, const void *b, size_t len);

/* Clears memory in a way the compiler will not optimise away (for keys and PINs). */
void secure_zero(void *p, size_t len);

#endif /* SHA256_H */
