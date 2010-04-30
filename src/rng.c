/*
 * Copyright (C) 2010 Julien BLACHE <jb@jblache.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdint.h>

#include <gcrypt.h>

#include "rng.h"


/* Park & Miller Minimal Standard PRNG
 * w/ Bays-Durham shuffle
 * From Numerical Recipes in C, 2nd ed.
 */
static int32_t
rng_rand_internal(int32_t *seed)
{
  int32_t hi;
  int32_t lo;
  int32_t res;

  hi = *seed / 127773;
  lo = *seed % 127773;

  res = 16807 * lo - 2836 * hi;

  if (res < 0)
    res += 0x7fffffffL; /* 2147483647 */

  *seed = res;

  return res;
}

void
rng_init(struct rng_ctx *ctx)
{
  int32_t val;
  int i;

  gcry_randomize(&ctx->seed, sizeof(ctx->seed), GCRY_STRONG_RANDOM);

  /* Load the shuffle array - first 8 iterations discarded */
  for (i = sizeof(ctx->iv) / sizeof(ctx->iv[0]) + 7; i >= 0; i--)
    {
      val = rng_rand_internal(&ctx->seed);

      if (i < sizeof(ctx->iv) / sizeof(ctx->iv[0]))
	ctx->iv[i] = val;
    }

  ctx->iy = ctx->iv[0];
}

int32_t
rng_rand(struct rng_ctx *ctx)
{
  int i;

  /* Select return value */
  i = ctx->iy / (1 + (0x7fffffffL - 1) / (sizeof(ctx->iv) / sizeof(ctx->iv[0])));
  ctx->iy = ctx->iv[i];

  /* Refill */
  ctx->iv[i] = rng_rand_internal(&ctx->seed);

  return ctx->iy;
}

/* Integer in [min, max[ */
/* Taken from GLib 2.0 v2.25.3, g_rand_int_range(), GPLv2+ */
int32_t
rng_rand_range(struct rng_ctx *ctx, int32_t min, int32_t max)
{
  int32_t res;
  int32_t dist;
  uint32_t maxvalue;
  uint32_t leftover;

  dist = max - min;

  if (dist <= 0)
    return min;

  /* maxvalue is set to the predecessor of the greatest
   * multiple of dist less or equal 2^32. */
  if (dist <= 0x80000000u) /* 2^31 */
    {
      /* maxvalue = 2^32 - 1 - (2^32 % dist) */
      leftover = (0x80000000u % dist) * 2;
      if (leftover >= dist)
	leftover -= dist;
      maxvalue = 0xffffffffu - leftover;
    }
  else
    maxvalue = dist - 1;
  do
    res = rng_rand(ctx);
  while (res > maxvalue);

  res %= dist;

  return min + res;
}

/* Fisher-Yates shuffling algorithm
 * Durstenfeld in-place shuffling variant
 */
void
shuffle_ptr(struct rng_ctx *ctx, void **values, int len)
{
  int i;
  int32_t j;
  void *tmp;

  for (i = len - 1; i > 0; i--)
    {
      j = rng_rand_range(ctx, 0, i + 1);

      tmp = values[i];
      values[i] = values[j];
      values[j] = tmp;
    }
}

