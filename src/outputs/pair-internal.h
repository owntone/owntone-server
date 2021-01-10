#include <stdint.h>
#include <sodium.h>

struct SRPUser;

struct pair_setup_context
{
  struct pair_definition *type;

  struct SRPUser *user;

  char pin[4];
  char device_id[17]; // Incl. zero term

  const uint8_t *pkA;
  int pkA_len;

  uint8_t *pkB;
  uint64_t pkB_len;

  const uint8_t *M1;
  int M1_len;

  uint8_t *M2;
  uint64_t M2_len;

  uint8_t *salt;
  uint64_t salt_len;
  uint8_t public_key[crypto_sign_PUBLICKEYBYTES];
  uint8_t private_key[crypto_sign_SECRETKEYBYTES];
  // Hex-formatet concatenation of public + private, 0-terminated
  char auth_key[2 * (crypto_sign_PUBLICKEYBYTES + crypto_sign_SECRETKEYBYTES) + 1];

  // We don't actually use the server's epk and authtag for anything
  uint8_t *epk;
  uint64_t epk_len;
  uint8_t *authtag;
  uint64_t authtag_len;

  int setup_is_completed;
  const char *errmsg;
};

struct pair_verify_context
{
  struct pair_definition *type;

  char device_id[17]; // Incl. zero term

  uint8_t server_eph_public_key[32];
  uint8_t server_public_key[64];

  uint8_t client_public_key[crypto_sign_PUBLICKEYBYTES];
  uint8_t client_private_key[crypto_sign_SECRETKEYBYTES];

  uint8_t client_eph_public_key[32];
  uint8_t client_eph_private_key[32];

  uint8_t shared_secret[32];

  int verify_is_completed;
  const char *errmsg;
};

struct pair_cipher_context
{
  struct pair_definition *type;

  uint8_t encryption_key[32];
  uint8_t decryption_key[32];

  uint64_t encryption_counter;
  uint64_t decryption_counter;

  const char *errmsg;
};

struct pair_definition
{
  struct pair_setup_context *(*pair_setup_new)(struct pair_definition *type, const char *pin, const char *device_id);
  void (*pair_setup_free)(struct pair_setup_context *sctx);
  int (*pair_setup_result)(const uint8_t **key, size_t *key_len, struct pair_setup_context *sctx);

  uint8_t *(*pair_setup_request1)(size_t *len, struct pair_setup_context *sctx);
  uint8_t *(*pair_setup_request2)(size_t *len, struct pair_setup_context *sctx);
  uint8_t *(*pair_setup_request3)(size_t *len, struct pair_setup_context *sctx);

  int (*pair_setup_response1)(struct pair_setup_context *sctx, const uint8_t *data, size_t data_len);
  int (*pair_setup_response2)(struct pair_setup_context *sctx, const uint8_t *data, size_t data_len);
  int (*pair_setup_response3)(struct pair_setup_context *sctx, const uint8_t *data, size_t data_len);

  uint8_t *(*pair_verify_request1)(size_t *len, struct pair_verify_context *vctx);
  uint8_t *(*pair_verify_request2)(size_t *len, struct pair_verify_context *vctx);

  int (*pair_verify_response1)(struct pair_verify_context *vctx, const uint8_t *data, size_t data_len);
  int (*pair_verify_response2)(struct pair_verify_context *vctx, const uint8_t *data, size_t data_len);

  struct pair_cipher_context *(*pair_cipher_new)(struct pair_definition *type, int channel, const uint8_t *shared_secret, size_t shared_secret_len);
  void (*pair_cipher_free)(struct pair_cipher_context *cctx);

  int (*pair_encrypt)(uint8_t **ciphertext, size_t *ciphertext_len, uint8_t *plaintext, size_t plaintext_len, struct pair_cipher_context *cctx);
  int (*pair_decrypt)(uint8_t **plaintext, size_t *plaintext_len, uint8_t *ciphertext, size_t ciphertext_len, struct pair_cipher_context *cctx);
};


/* -------------------- GCRYPT AND OPENSSL COMPABILITY --------------------- */
/*                   partly borrowed from ffmpeg (rtmpdh.c)                  */

#if CONFIG_GCRYPT
#include <gcrypt.h>
#define SHA512_DIGEST_LENGTH 64
#define bnum_new(bn)                                            \
    do {                                                        \
        if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) { \
            if (!gcry_check_version("1.5.4"))                   \
                abort();                                        \
            gcry_control(GCRYCTL_DISABLE_SECMEM, 0);            \
            gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);   \
        }                                                       \
        bn = gcry_mpi_new(1);                                   \
    } while (0)
#define bnum_free(bn)                 gcry_mpi_release(bn)
#define bnum_num_bytes(bn)            (gcry_mpi_get_nbits(bn) + 7) / 8
#define bnum_is_zero(bn)              (gcry_mpi_cmp_ui(bn, (unsigned long)0) == 0)
#define bnum_bn2bin(bn, buf, len)     gcry_mpi_print(GCRYMPI_FMT_USG, buf, len, NULL, bn)
#define bnum_bin2bn(bn, buf, len)     gcry_mpi_scan(&bn, GCRYMPI_FMT_USG, buf, len, NULL)
#define bnum_hex2bn(bn, buf)          gcry_mpi_scan(&bn, GCRYMPI_FMT_HEX, buf, 0, 0)
#define bnum_random(bn, num_bits)     gcry_mpi_randomize(bn, num_bits, GCRY_WEAK_RANDOM)
#define bnum_add(bn, a, b)            gcry_mpi_add(bn, a, b)
#define bnum_sub(bn, a, b)            gcry_mpi_sub(bn, a, b)
#define bnum_mul(bn, a, b)            gcry_mpi_mul(bn, a, b)
typedef gcry_mpi_t bnum;
__attribute__((unused)) static void bnum_modexp(bnum bn, bnum y, bnum q, bnum p)
{
  gcry_mpi_powm(bn, y, q, p);
}
#elif CONFIG_OPENSSL
#include <openssl/crypto.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#define bnum_new(bn)                  bn = BN_new()
#define bnum_free(bn)                 BN_free(bn)
#define bnum_num_bytes(bn)            BN_num_bytes(bn)
#define bnum_is_zero(bn)              BN_is_zero(bn)
#define bnum_bn2bin(bn, buf, len)     BN_bn2bin(bn, buf)
#define bnum_bin2bn(bn, buf, len)     bn = BN_bin2bn(buf, len, 0)
#define bnum_hex2bn(bn, buf)          BN_hex2bn(&bn, buf)
#define bnum_random(bn, num_bits)     BN_rand(bn, num_bits, 0, 0)
#define bnum_add(bn, a, b)            BN_add(bn, a, b)
#define bnum_sub(bn, a, b)            BN_sub(bn, a, b)
typedef BIGNUM* bnum;
__attribute__((unused)) static void bnum_mul(bnum bn, bnum a, bnum b)
{
  // No error handling
  BN_CTX *ctx = BN_CTX_new();
  BN_mul(bn, a, b, ctx);
  BN_CTX_free(ctx);
}
__attribute__((unused)) static void bnum_modexp(bnum bn, bnum y, bnum q, bnum p)
{
  // No error handling
  BN_CTX *ctx = BN_CTX_new();
  BN_mod_exp(bn, y, q, p, ctx);
  BN_CTX_free(ctx);
}
#endif


/* -------------------------- SHARED HASHING HELPERS ------------------------ */

#ifdef CONFIG_OPENSSL
enum hash_alg
{
  HASH_SHA1,
  HASH_SHA224,
  HASH_SHA256,
  HASH_SHA384,
  HASH_SHA512,
};
#elif CONFIG_GCRYPT
enum hash_alg
{
  HASH_SHA1 = GCRY_MD_SHA1,
  HASH_SHA224 = GCRY_MD_SHA224,
  HASH_SHA256 = GCRY_MD_SHA256,
  HASH_SHA384 = GCRY_MD_SHA384,
  HASH_SHA512 = GCRY_MD_SHA512,
};
#endif

#if CONFIG_OPENSSL
typedef union
{
  SHA_CTX    sha;
  SHA256_CTX sha256;
  SHA512_CTX sha512;
} HashCTX;
#elif CONFIG_GCRYPT
typedef gcry_md_hd_t HashCTX;
#endif

int
hash_init(enum hash_alg alg, HashCTX *c);

int
hash_update(enum hash_alg alg, HashCTX *c, const void *data, size_t len);

int
hash_final(enum hash_alg alg, HashCTX *c, unsigned char *md);

unsigned char *
hash(enum hash_alg alg, const unsigned char *d, size_t n, unsigned char *md);

int
hash_length(enum hash_alg alg);

int
hash_ab(enum hash_alg alg, unsigned char *md, const unsigned char *m1, int m1_len, const unsigned char *m2, int m2_len);

bnum
H_nn_pad(enum hash_alg alg, const bnum n1, const bnum n2);

bnum
H_ns(enum hash_alg alg, const bnum n, const unsigned char *bytes, int len_bytes);

void
update_hash_n(enum hash_alg alg, HashCTX *ctx, const bnum n);

void
hash_num(enum hash_alg alg, const bnum n, unsigned char *dest);


/* ----------------------------- OTHER HELPERS -------------------------------*/

#ifdef DEBUG_PAIR
void
hexdump(const char *msg, uint8_t *mem, size_t len);
#endif
