#include <string.h>

#include "check.h"
#include "sha256.h"

static void to_hex(const unsigned char *bytes, size_t len, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * len] = '\0';
}

static void check_sha256(const char *msg, const char *expected) {
    unsigned char digest[SHA256_DIGEST_SIZE];
    char hex[2 * SHA256_DIGEST_SIZE + 1];
    sha256(msg, strlen(msg), digest);
    to_hex(digest, sizeof digest, hex);
    CHECK_STR_EQ(hex, expected);
}

/* FIPS 180-4 examples, plus the one-million-'a' message fed in uneven pieces. */
static void test_sha256(void) {
    check_sha256("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check_sha256("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    char chunk[997];
    memset(chunk, 'a', sizeof chunk);
    Sha256 ctx;
    sha256_init(&ctx);
    size_t left = 1000000;
    while (left > 0) {
        size_t n = left < sizeof chunk ? left : sizeof chunk;
        sha256_update(&ctx, chunk, n);
        left -= n;
    }
    unsigned char digest[SHA256_DIGEST_SIZE];
    char hex[2 * SHA256_DIGEST_SIZE + 1];
    sha256_final(&ctx, digest);
    to_hex(digest, sizeof digest, hex);
    CHECK_STR_EQ(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void check_hmac(const void *key, size_t key_len, const char *msg, const char *expected) {
    unsigned char mac[SHA256_DIGEST_SIZE];
    char hex[2 * SHA256_DIGEST_SIZE + 1];
    hmac_sha256(key, key_len, msg, strlen(msg), mac);
    to_hex(mac, sizeof mac, hex);
    CHECK_STR_EQ(hex, expected);
}

/* RFC 4231 test cases 1, 2 and 6 (a key longer than the block size). */
static void test_hmac(void) {
    unsigned char key[131];
    memset(key, 0x0b, 20);
    check_hmac(key, 20, "Hi There",
               "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    check_hmac("Jefe", 4, "what do ya want for nothing?",
               "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    memset(key, 0xaa, sizeof key);
    check_hmac(key, sizeof key, "Test Using Larger Than Block-Size Key - Hash Key First",
               "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

static void check_pbkdf2(const char *password, size_t password_len, const char *salt,
                         size_t salt_len, uint32_t iterations, const char *expected) {
    size_t len = strlen(expected) / 2;
    unsigned char out[64];
    char hex[2 * sizeof out + 1];
    pbkdf2_hmac_sha256(password, password_len, salt, salt_len, iterations, out, len);
    to_hex(out, len, hex);
    CHECK_STR_EQ(hex, expected);
}

/* RFC 7914 section 11, and the widely used vectors from draft-josefsson-pbkdf2-test-vectors
 * recomputed for SHA-256 (output lengths that are not a multiple of the digest size and
 * passwords/salts containing NUL bytes). */
static void test_pbkdf2(void) {
    check_pbkdf2("passwd", 6, "salt", 4, 1,
                 "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
                 "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
    check_pbkdf2("Password", 8, "NaCl", 4, 80000,
                 "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
                 "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
    check_pbkdf2("password", 8, "salt", 4, 1,
                 "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    check_pbkdf2("password", 8, "salt", 4, 2,
                 "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    check_pbkdf2("password", 8, "salt", 4, 4096,
                 "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    check_pbkdf2(
        "passwordPASSWORDpassword", 24, "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096,
        "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9");
    check_pbkdf2("pass\0word", 9, "sa\0lt", 5, 4096, "89b69d0516f829893c696226650a8687");
}

static void test_helpers(void) {
    CHECK(bytes_equal_ct("abcd", "abcd", 4));
    CHECK(!bytes_equal_ct("abcd", "abce", 4));
    CHECK(!bytes_equal_ct("xbcd", "abcd", 4));
    CHECK(bytes_equal_ct("a", "b", 0));

    unsigned char buf[8];
    memset(buf, 0x5a, sizeof buf);
    secure_zero(buf, sizeof buf);
    static const unsigned char zeros[8];
    CHECK(memcmp(buf, zeros, sizeof buf) == 0);
}

void suite_sha256(void) {
    test_sha256();
    test_hmac();
    test_pbkdf2();
    test_helpers();
}
