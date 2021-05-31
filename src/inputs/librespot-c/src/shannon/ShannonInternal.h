#ifndef SHANNONINTERNAL_H
#define SHANNONINTERNAL_H

#include "Shannon.h"

#include <limits.h>

#define N SHANNON_N
#define WORDSIZE 32
#define UCHAR unsigned char

#define WORD uint32_t
#define WORD_MAX UINT32_MAX

#if WORD_MAX == 0xffffffff
#define ROTL(w,x) (((w) << (x))|((w) >> (32 - (x))))
#define ROTR(w,x) (((w) >> (x))|((w) << (32 - (x))))
#else
#define ROTL(w,x) (((w) << (x))|(((w) & 0xffffffff) >> (32 - (x))))
#define ROTR(w,x) ((((w) & 0xffffffff) >> (x))|((w) << (32 - (x))))
#endif

#endif
