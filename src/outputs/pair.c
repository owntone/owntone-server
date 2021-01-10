/*
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // for isprint()

#include <sodium.h>

#include "pair.h"
#include "pair-internal.h"

extern struct pair_definition pair_fruit;
extern struct pair_definition pair_homekit_normal;
extern struct pair_definition pair_homekit_transient;

// Must be in sync with enum pair_type
static struct pair_definition *pair[] = {
    &pair_fruit,
    &pair_homekit_normal,
    &pair_homekit_transient,
};

/* -------------------------- SHARED HASHING HELPERS ------------------------ */

int
hash_init(enum hash_alg alg, HashCTX *c)
{
#if CONFIG_OPENSSL
  switch (alg)
    {
      case HASH_SHA1  : return SHA1_Init(&c->sha);
      case HASH_SHA224: return SHA224_Init(&c->sha256);
      case HASH_SHA256: return SHA256_Init(&c->sha256);
      case HASH_SHA384: return SHA384_Init(&c->sha512);
      case HASH_SHA512: return SHA512_Init(&c->sha512);
      default:
        return -1;
    };
#elif CONFIG_GCRYPT
  gcry_error_t err;

  err = gcry_md_open(c, alg, 0);

  if (err)
    return -1;

  return 0;
#endif
}

int
hash_update(enum hash_alg alg, HashCTX *c, const void *data, size_t len)
{
#if CONFIG_OPENSSL
  switch (alg)
    {
      case HASH_SHA1  : return SHA1_Update(&c->sha, data, len);
      case HASH_SHA224: return SHA224_Update(&c->sha256, data, len);
      case HASH_SHA256: return SHA256_Update(&c->sha256, data, len);
      case HASH_SHA384: return SHA384_Update(&c->sha512, data, len);
      case HASH_SHA512: return SHA512_Update(&c->sha512, data, len);
      default:
        return -1;
    };
#elif CONFIG_GCRYPT
  gcry_md_write(*c, data, len);
  return 0;
#endif
}

int
hash_final(enum hash_alg alg, HashCTX *c, unsigned char *md)
{
#if CONFIG_OPENSSL
  switch (alg)
    {
      case HASH_SHA1  : return SHA1_Final(md, &c->sha);
      case HASH_SHA224: return SHA224_Final(md, &c->sha256);
      case HASH_SHA256: return SHA256_Final(md, &c->sha256);
      case HASH_SHA384: return SHA384_Final(md, &c->sha512);
      case HASH_SHA512: return SHA512_Final(md, &c->sha512);
      default:
        return -1;
    };
#elif CONFIG_GCRYPT
  unsigned char *buf = gcry_md_read(*c, alg);
  if (!buf)
    return -1;

  memcpy(md, buf, gcry_md_get_algo_dlen(alg));
  gcry_md_close(*c);
  return 0;
#endif
}

unsigned char *
hash(enum hash_alg alg, const unsigned char *d, size_t n, unsigned char *md)
{
#if CONFIG_OPENSSL
  switch (alg)
    {
      case HASH_SHA1  : return SHA1(d, n, md);
      case HASH_SHA224: return SHA224(d, n, md);
      case HASH_SHA256: return SHA256(d, n, md);
      case HASH_SHA384: return SHA384(d, n, md);
      case HASH_SHA512: return SHA512(d, n, md);
      default:
        return NULL;
    };
#elif CONFIG_GCRYPT
  gcry_md_hash_buffer(alg, md, d, n);
  return md;
#endif
}

int
hash_length(enum hash_alg alg)
{
#if CONFIG_OPENSSL
  switch (alg)
    {
      case HASH_SHA1  : return SHA_DIGEST_LENGTH;
      case HASH_SHA224: return SHA224_DIGEST_LENGTH;
      case HASH_SHA256: return SHA256_DIGEST_LENGTH;
      case HASH_SHA384: return SHA384_DIGEST_LENGTH;
      case HASH_SHA512: return SHA512_DIGEST_LENGTH;
      default:
        return -1;
    };
#elif CONFIG_GCRYPT
  return gcry_md_get_algo_dlen(alg);
#endif
}

int
hash_ab(enum hash_alg alg, unsigned char *md, const unsigned char *m1, int m1_len, const unsigned char *m2, int m2_len)
{
  HashCTX ctx;

  hash_init(alg, &ctx);
  hash_update(alg, &ctx, m1, m1_len);
  hash_update(alg, &ctx, m2, m2_len);
  return hash_final(alg, &ctx, md);
}

bnum
H_nn_pad(enum hash_alg alg, const bnum n1, const bnum n2)
{
  bnum          bn;
  unsigned char *bin;
  unsigned char buff[SHA512_DIGEST_LENGTH];
  int           len_n1 = bnum_num_bytes(n1);
  int           len_n2 = bnum_num_bytes(n2);
  int           nbytes = 2 * len_n1;

  if ((len_n2 < 1) || (len_n2 > len_n1))
    return 0;

  bin = calloc( 1, nbytes );

  bnum_bn2bin(n1, bin, len_n1);
  bnum_bn2bin(n2, bin + nbytes - len_n2, len_n2);
  hash( alg, bin, nbytes, buff );
  free(bin);
  bnum_bin2bn(bn, buff, hash_length(alg));
  return bn;
}

bnum
H_ns(enum hash_alg alg, const bnum n, const unsigned char *bytes, int len_bytes)
{
  bnum          bn;
  unsigned char buff[SHA512_DIGEST_LENGTH];
  int           len_n  = bnum_num_bytes(n);
  int           nbytes = len_n + len_bytes;
  unsigned char *bin   = malloc(nbytes);

  bnum_bn2bin(n, bin, len_n);
  memcpy( bin + len_n, bytes, len_bytes );
  hash( alg, bin, nbytes, buff );
  free(bin);
  bnum_bin2bn(bn, buff, hash_length(alg));
  return bn;
}

void
update_hash_n(enum hash_alg alg, HashCTX *ctx, const bnum n)
{
  unsigned long len = bnum_num_bytes(n);
  unsigned char *n_bytes = malloc(len);

  bnum_bn2bin(n, n_bytes, len);
  hash_update(alg, ctx, n_bytes, len);
  free(n_bytes);
}

void
hash_num(enum hash_alg alg, const bnum n, unsigned char *dest)
{
  int           nbytes = bnum_num_bytes(n);
  unsigned char *bin   = malloc(nbytes);

  bnum_bn2bin(n, bin, nbytes);
  hash( alg, bin, nbytes, dest );
  free(bin);
}


/* ----------------------------- OTHER HELPERS -------------------------------*/

#ifdef DEBUG_PAIR
void
hexdump(const char *msg, uint8_t *mem, size_t len)
{
  int i, j;
  int hexdump_cols = 16;

  if (msg)
    printf("%s", msg);

  for (i = 0; i < len + ((len % hexdump_cols) ? (hexdump_cols - len % hexdump_cols) : 0); i++)
    {
      if(i % hexdump_cols == 0)
	printf("0x%06x: ", i);

      if (i < len)
	printf("%02x ", 0xFF & ((char*)mem)[i]);
      else
	printf("   ");

      if (i % hexdump_cols == (hexdump_cols - 1))
	{
	  for (j = i - (hexdump_cols - 1); j <= i; j++)
	    {
	      if (j >= len)
		putchar(' ');
	      else if (isprint(((char*)mem)[j]))
		putchar(0xFF & ((char*)mem)[j]);
	      else
		putchar('.');
	    }

	  putchar('\n');
	}
    }
}
#endif


/* ----------------------------------- API -----------------------------------*/

struct pair_setup_context *
pair_setup_new(enum pair_type type, const char *pin, const char *device_id)
{
  if (!pair[type]->pair_setup_new)
    return NULL;

  return pair[type]->pair_setup_new(pair[type], pin, device_id);
}

void
pair_setup_free(struct pair_setup_context *sctx)
{
  if (!sctx)
    return;

  if (!sctx->type->pair_setup_free)
    return;

  return sctx->type->pair_setup_free(sctx);
}

const char *
pair_setup_errmsg(struct pair_setup_context *sctx)
{
  return sctx->errmsg;
}

uint8_t *
pair_setup_request1(size_t *len, struct pair_setup_context *sctx)
{
  if (!sctx->type->pair_setup_request1)
    return NULL;

  return sctx->type->pair_setup_request1(len, sctx);

  if (!sctx->type->pair_setup_request1)
    return NULL;

  return sctx->type->pair_setup_request1(len, sctx);
}

uint8_t *
pair_setup_request2(size_t *len, struct pair_setup_context *sctx)
{
  if (!sctx->type->pair_setup_request2)
    return NULL;

  return sctx->type->pair_setup_request2(len, sctx);
}

uint8_t *
pair_setup_request3(size_t *len, struct pair_setup_context *sctx)
{
  if (!sctx->type->pair_setup_request3)
    return NULL;

  return sctx->type->pair_setup_request3(len, sctx);
}

int
pair_setup_response1(struct pair_setup_context *sctx, const uint8_t *data, size_t data_len)
{
  if (!sctx->type->pair_setup_response1)
    return -1;

  return sctx->type->pair_setup_response1(sctx, data, data_len);
}

int
pair_setup_response2(struct pair_setup_context *sctx, const uint8_t *data, size_t data_len)
{
  if (!sctx->type->pair_setup_response2)
    return -1;

  return sctx->type->pair_setup_response2(sctx, data, data_len);
}

int
pair_setup_response3(struct pair_setup_context *sctx, const uint8_t *data, size_t data_len)
{
  if (!sctx->type->pair_setup_response3)
    return -1;

  if (sctx->type->pair_setup_response3(sctx, data, data_len) != 0)
    return -1;

  return 0;
}

int
pair_setup_result(const char **hexkey, const uint8_t **key, size_t *key_len, struct pair_setup_context *sctx)
{
  const uint8_t *out_key;
  size_t out_len;
  char *ptr;
  int i;

  if (!sctx->setup_is_completed)
    {
      sctx->errmsg = "Setup result: The pair setup has not been completed";
      return -1;
    }

  if (!sctx->type->pair_setup_result)
    return -1;

  if (sctx->type->pair_setup_result(&out_key, &out_len, sctx) != 0)
    return -1;

  if (2 * out_len + 1 > sizeof(sctx->auth_key))
    return -1;

  ptr = sctx->auth_key;
  for (i = 0; i < out_len; i++)
    ptr += sprintf(ptr, "%02x", out_key[i]);
  *ptr = '\0';

  if (key)
    *key = out_key;
  if (key_len)
    *key_len = out_len;
  if (hexkey)
    *hexkey = sctx->auth_key;

  return 0;
}


struct pair_verify_context *
pair_verify_new(enum pair_type type, const char *hexkey, const char *device_id)
{
  struct pair_verify_context *vctx;
  char hex[] = { 0, 0, 0 };
  size_t hexkey_len;
  const char *ptr;
  int i;

  if (sodium_init() == -1)
    return NULL;

  if (!hexkey)
    return NULL;

  hexkey_len = strlen(hexkey);

  if (hexkey_len != 2 * sizeof(vctx->client_private_key))
    return NULL;

  if (device_id && strlen(device_id) != 16)
    return NULL;

  vctx = calloc(1, sizeof(struct pair_verify_context));
  if (!vctx)
    return NULL;

  vctx->type = pair[type];

  if (device_id)
    memcpy(vctx->device_id, device_id, strlen(device_id));

  ptr = hexkey;
  for (i = 0; i < sizeof(vctx->client_private_key); i++, ptr+=2)
    {
      hex[0] = ptr[0];
      hex[1] = ptr[1];
      vctx->client_private_key[i] = strtol(hex, NULL, 16);
    }

  ptr = hexkey + hexkey_len - 2 * sizeof(vctx->client_public_key);
  for (i = 0; i < sizeof(vctx->client_public_key); i++, ptr+=2)
    {
      hex[0] = ptr[0];
      hex[1] = ptr[1];
      vctx->client_public_key[i] = strtol(hex, NULL, 16);
    }

  return vctx;
}

void
pair_verify_free(struct pair_verify_context *vctx)
{
  if (!vctx)
    return;

  free(vctx);
}

const char *
pair_verify_errmsg(struct pair_verify_context *vctx)
{
  return vctx->errmsg;
}

uint8_t *
pair_verify_request1(size_t *len, struct pair_verify_context *vctx)
{
  if (!vctx->type->pair_verify_request1)
    return NULL;

  return vctx->type->pair_verify_request1(len, vctx);
}

uint8_t *
pair_verify_request2(size_t *len, struct pair_verify_context *vctx)
{
  if (!vctx->type->pair_verify_request2)
    return NULL;

  return vctx->type->pair_verify_request2(len, vctx);
}

int
pair_verify_response1(struct pair_verify_context *vctx, const uint8_t *data, size_t data_len)
{
  if (!vctx->type->pair_verify_response1)
    return -1;

  return vctx->type->pair_verify_response1(vctx, data, data_len);
}

int
pair_verify_response2(struct pair_verify_context *vctx, const uint8_t *data, size_t data_len)
{
  if (!vctx->type->pair_verify_response2)
    return -1;

  if (vctx->type->pair_verify_response2(vctx, data, data_len) != 0)
    return -1;

  vctx->verify_is_completed = 1;
  return 0;
}

int
pair_verify_result(const uint8_t **shared_secret, size_t *shared_secret_len, struct pair_verify_context *vctx)
{
  if (!vctx->verify_is_completed)
    {
      vctx->errmsg = "Verify result: The pairing verification did not complete";
      return -1;
    }

  *shared_secret = vctx->shared_secret;
  *shared_secret_len = sizeof(vctx->shared_secret);

  return 0;
}

struct pair_cipher_context *
pair_cipher_new(enum pair_type type, int channel, const uint8_t *shared_secret, size_t shared_secret_len)
{
  if (!pair[type]->pair_cipher_new)
    return NULL;

  return pair[type]->pair_cipher_new(pair[type], channel, shared_secret, shared_secret_len);
}

void
pair_cipher_free(struct pair_cipher_context *cctx)
{
  if (!cctx)
    return;

  if (!cctx->type->pair_cipher_free)
    return;

  return cctx->type->pair_cipher_free(cctx);
}

const char *
pair_cipher_errmsg(struct pair_cipher_context *cctx)
{
  return cctx->errmsg;
}

int
pair_encrypt(uint8_t **ciphertext, size_t *ciphertext_len, uint8_t *plaintext, size_t plaintext_len, struct pair_cipher_context *cctx)
{
  if (!cctx->type->pair_encrypt)
    return 0;

  return cctx->type->pair_encrypt(ciphertext, ciphertext_len, plaintext, plaintext_len, cctx);
}

int
pair_decrypt(uint8_t **plaintext, size_t *plaintext_len, uint8_t *ciphertext, size_t ciphertext_len, struct pair_cipher_context *cctx)
{
  if (!cctx->type->pair_decrypt)
    return 0;

  return cctx->type->pair_decrypt(plaintext, plaintext_len, ciphertext, ciphertext_len, cctx);
}

void
pair_encrypt_rollback(struct pair_cipher_context *cctx)
{
  cctx->encryption_counter--;
}

void
pair_decrypt_rollback(struct pair_cipher_context *cctx)
{
  cctx->decryption_counter--;
}
