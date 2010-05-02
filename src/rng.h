
#ifndef __RNG_H__
#define __RNG_H__

struct rng_ctx {
  int32_t iy;
  int32_t iv[32]; /* shuffle array */
  int32_t seed;
};


void
rng_init(struct rng_ctx *ctx);

int32_t
rng_rand(struct rng_ctx *ctx);

int32_t
rng_rand_range(struct rng_ctx *ctx, int32_t min, int32_t max);

void
shuffle_ptr(struct rng_ctx *ctx, void **values, int len);

#endif /* !__RNG_H__ */

