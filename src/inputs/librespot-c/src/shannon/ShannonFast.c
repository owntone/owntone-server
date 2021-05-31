/* $Id: shnfast.c 442 2006-05-12 23:22:21Z ggr $ */
/* ShannonFast: Shannon stream cipher and MAC -- fast implementation */

/*
THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE AND AGAINST
INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* interface, multiplication table and SBox */
#include <stdlib.h>
#include <string.h>
#include "ShannonInternal.h"

/*
 * FOLD is how many register cycles need to be performed after combining the
 * last byte of key and non-linear feedback, before every byte depends on every
 * byte of the key. This depends on the feedback and nonlinear functions, and
 * on where they are combined into the register. Making it same as the
 * register length is a safe and conservative choice.
 */
#define FOLD N		/* how many iterations of folding to do */
#define INITKONST 0x6996c53a /* value of KONST to use during key loading */
#define KEYP 13		/* where to insert key/MAC words */

#define Byte(x,i) ((UCHAR)(((x) >> (8*i)) & 0xFF))

/* define IS_LITTLE_ENDIAN for faster operation when appropriate */
#ifdef IS_LITTLE_ENDIAN
/* Useful macros -- little endian words on a little endian machine */
#define BYTE2WORD(b) (*(WORD *)(b))
#define WORD2BYTE(w, b) ((*(WORD *)(b)) = w)
#define XORWORD(w, b) ((*(WORD *)(b)) ^= w)
#else
/* Useful macros -- machine independent little-endian version */
#define BYTE2WORD(b) ( \
	(((WORD)(b)[3] & 0xFF)<<24) | \
	(((WORD)(b)[2] & 0xFF)<<16) | \
	(((WORD)(b)[1] & 0xFF)<<8) | \
	(((WORD)(b)[0] & 0xFF)) \
)
#define WORD2BYTE(w, b) { \
	(b)[3] = Byte(w,3); \
	(b)[2] = Byte(w,2); \
	(b)[1] = Byte(w,1); \
	(b)[0] = Byte(w,0); \
}
#define XORWORD(w, b) { \
	(b)[3] ^= Byte(w,3); \
	(b)[2] ^= Byte(w,2); \
	(b)[1] ^= Byte(w,1); \
	(b)[0] ^= Byte(w,0); \
}
#endif

/* give correct offset for the current position of the register,
 * where logically R[0] is at position "zero". Note that this works for
 * both the stream register and the CRC register.
 */
#define OFF(zero, i) (((zero)+(i)) % N)

/* step the shift register */
/* After stepping, "zero" moves right one place */
#define STEP(c,z) \
    { \
	t = c->R[OFF(z,12)] ^ c->R[OFF(z,13)] ^ c->konst; \
	/* Sbox 1 */ \
	t ^= ROTL(t, 5)  | ROTL(t, 7); \
	t ^= ROTL(t, 19) | ROTL(t, 22); \
	c->R[OFF(z,0)] = t ^ ROTL(c->R[OFF(z,0)],1); \
	t = c->R[OFF((z+1),2)] ^ c->R[OFF((z+1),15)]; \
	/* Sbox 2 */ \
	t ^= ROTL(t, 7)  | ROTL(t, 22); \
	t ^= ROTL(t, 5) | ROTL(t, 19); \
	c->R[OFF((z+1),0)] ^= t; \
	c->sbuf = t ^ c->R[OFF((z+1),8)] ^ c->R[OFF((z+1),12)]; \
    }

static void
cycle(shn_ctx *c)
{
    WORD	t;
    int		i;

    /* nonlinear feedback function */
    STEP(c,0);
    /* shift register */
    t = c->R[0];
    for (i = 1; i < N; ++i)
	c->R[i-1] = c->R[i];
    c->R[N-1] = t;
}

/* The Shannon MAC function is modelled after the concepts of Phelix and SHA.
 * Basically, words to be accumulated in the MAC are incorporated in two
 * different ways:
 * 1. They are incorporated into the stream cipher register at a place
 *    where they will immediately have a nonlinear effect on the state
 * 2. They are incorporated into bit-parallel CRC-16 registers; the
 *    contents of these registers will be used in MAC finalization.
 */


/* Accumulate a CRC of input words, later to be fed into MAC.
 * This is actually 32 parallel CRC-16s, using the IBM CRC-16
 * polynomial x^16 + x^15 + x^2 + 1.
 */
#define CRCFUNC(c,i,z) \
    { \
	c->CRC[OFF(z,0)] ^= c->CRC[OFF(z,2)] ^ c->CRC[OFF(z,15)] ^ i; \
    }

static void
crcfunc(shn_ctx *c, WORD i)
{
    WORD    t;

    CRCFUNC(c, i, 0);
    /* now correct alignment of CRC accumulator */
    t = c->CRC[0];
    for (i = 1; i < N; ++i)
	c->CRC[i-1] = c->CRC[i];
    c->CRC[N-1] = t;
}

/* Normal MAC word processing: do both SHA and CRC.
 */
static void
macfunc(shn_ctx *c, WORD i)
{
    crcfunc(c, i);
    c->R[KEYP] ^= i;
}

/* initialise to known state
 */
static void
shn_initstate(shn_ctx *c)
{
    int		i;

    /* Register initialised to Fibonacci numbers; Counter zeroed. */
    c->R[0] = 1;
    c->R[1] = 1;
    for (i = 2; i < N; ++i)
	c->R[i] = c->R[i-1] + c->R[i-2];
    c->konst = INITKONST;
}

/* Save the current register state
 */
static void
shn_savestate(shn_ctx *c)
{
    int		i;

    for (i = 0; i < N; ++i)
	c->initR[i] = c->R[i];
}

/* initialise to previously saved register state
 */
static void
shn_reloadstate(shn_ctx *c)
{
    int		i;

    for (i = 0; i < N; ++i)
	c->R[i] = c->initR[i];
}

/* Initialise "konst"
 */
static void
shn_genkonst(shn_ctx *c)
{
    c->konst = c->R[0];
}

/* Load key material into the register
 */
#define ADDKEY(k) \
	c->R[KEYP] ^= (k);

/* nonlinear diffusion of register for key and MAC */
#define DROUND(z) { register WORD t; STEP(c,z); }
static void
shn_diffuse(shn_ctx *c)
{
    /* relies on FOLD == N! */
    DROUND(0);
    DROUND(1);
    DROUND(2);
    DROUND(3);
    DROUND(4);
    DROUND(5);
    DROUND(6);
    DROUND(7);
    DROUND(8);
    DROUND(9);
    DROUND(10);
    DROUND(11);
    DROUND(12);
    DROUND(13);
    DROUND(14);
    DROUND(15);
}

/* common actions for loading key material
 * Allow non-word-multiple key and nonce materianl
 * Note also initializes the CRC register as a side effect.
 */
static void
shn_loadkey(shn_ctx *c, const uint8_t key[], int keylen)
{
    int		i, j;
    WORD	k;
    uint8_t	xtra[4];

    /* start folding in key */
    for (i = 0; i < (keylen & ~0x3); i += 4)
    {
	k = BYTE2WORD(&key[i]);
	ADDKEY(k);
        cycle(c);
    }

    /* if there were any extra key bytes, zero pad to a word */
    if (i < keylen) {
	for (j = 0 /* i unchanged */; i < keylen; ++i)
	    xtra[j++] = key[i];
	for (/* j unchanged */; j < 4; ++j)
	    xtra[j] = 0;
	k = BYTE2WORD(xtra);
	ADDKEY(k);
        cycle(c);
    }

    /* also fold in the length of the key */
    ADDKEY(keylen);
    cycle(c);

    /* save a copy of the register */
    for (i = 0; i < N; ++i)
	c->CRC[i] = c->R[i];

    /* now diffuse */
    shn_diffuse(c);

    /* now xor the copy back -- makes key loading irreversible */
    for (i = 0; i < N; ++i)
	c->R[i] ^= c->CRC[i];
}

/* Published "key" interface
 */
void
shn_key(shn_ctx *c, const uint8_t key[], int keylen)
{
    shn_initstate(c);
    shn_loadkey(c, key, keylen);
    shn_genkonst(c);
    shn_savestate(c);
    c->nbuf = 0;
}

/* Published "nonce" interface
 */
void
shn_nonce(shn_ctx *c, const uint8_t nonce[], int noncelen)
{
    shn_reloadstate(c);
    c->konst = INITKONST;
    shn_loadkey(c, nonce, noncelen);
    shn_genkonst(c);
    c->nbuf = 0;
}

/* XOR pseudo-random bytes into buffer
 * Note: doesn't play well with MAC functions.
 */
#define SROUND(z) \
	{ register WORD t; \
	    STEP(c,z); \
	    XORWORD(c->sbuf, buf+(z*4)); \
	}
void
shn_stream(shn_ctx *c, uint8_t *buf, int nbytes)
{
    /* handle any previously buffered bytes */
    while (c->nbuf != 0 && nbytes != 0) {
	*buf++ ^= c->sbuf & 0xFF;
	c->sbuf >>= 8;
	c->nbuf -= 8;
	--nbytes;
    }

    /* do lots at a time, if there's enough to do */
    while (nbytes >= N*4)
    {
	SROUND(0);
	SROUND(1);
	SROUND(2);
	SROUND(3);
	SROUND(4);
	SROUND(5);
	SROUND(6);
	SROUND(7);
	SROUND(8);
	SROUND(9);
	SROUND(10);
	SROUND(11);
	SROUND(12);
	SROUND(13);
	SROUND(14);
	SROUND(15);
	buf += 4*N;
	nbytes -= N*4;
    }

    /* do small or odd size buffers the slow way */
    while (4 <= nbytes) {
	cycle(c);
	XORWORD(c->sbuf, buf);
	buf += 4;
	nbytes -= 4;
    }

    /* handle any trailing bytes */
    if (nbytes != 0) {
	cycle(c);
	c->nbuf = 32;
	while (c->nbuf != 0 && nbytes != 0) {
	    *buf++ ^= c->sbuf & 0xFF;
	    c->sbuf >>= 8;
	    c->nbuf -= 8;
	    --nbytes;
	}
    }
}

/* accumulate words into MAC without encryption
 * Note that plaintext is accumulated for MAC.
 */
#define MROUND(z) \
    { register WORD t, t1; \
	t1 = BYTE2WORD(buf+(z*4)); \
	STEP(c,z); \
	CRCFUNC(c,t1,z); \
	c->R[OFF(z+1,KEYP)] ^= t1; \
    }
void
shn_maconly(shn_ctx *c, uint8_t *buf, int nbytes)
{
    /* handle any previously buffered bytes */
    if (c->nbuf != 0) {
	while (c->nbuf != 0 && nbytes != 0) {
	    c->mbuf ^= (*buf++) << (32 - c->nbuf);
	    c->nbuf -= 8;
	    --nbytes;
	}
	if (c->nbuf != 0) /* not a whole word yet */
	    return;
	/* LFSR already cycled */
	macfunc(c, c->mbuf);
    }

    /* do lots at a time, if there's enough to do */
    while (4*N <= nbytes)
    {
	MROUND( 0);
	MROUND( 1);
	MROUND( 2);
	MROUND( 3);
	MROUND( 4);
	MROUND( 5);
	MROUND( 6);
	MROUND( 7);
	MROUND( 8);
	MROUND( 9);
	MROUND(10);
	MROUND(11);
	MROUND(12);
	MROUND(13);
	MROUND(14);
	MROUND(15);
	buf += 4*N;
	nbytes -= 4*N;
    }

    /* do small or odd size buffers the slow way */
    while (4 <= nbytes) {
	cycle(c);
	macfunc(c, BYTE2WORD(buf));
	buf += 4;
	nbytes -= 4;
    }

    /* handle any trailing bytes */
    if (nbytes != 0) {
	cycle(c);
	c->mbuf = 0;
	c->nbuf = 32;
	while (nbytes != 0) {
	    c->mbuf ^= (*buf++) << (32 - c->nbuf);
	    c->nbuf -= 8;
	    --nbytes;
	}
    }
}

/* Combined MAC and encryption.
 * Note that plaintext is accumulated for MAC.
 */
#define EROUND(z) \
    { register WORD t, t3; \
	STEP(c,z); \
	t3 = BYTE2WORD(buf+(z*4)); \
	CRCFUNC(c,t3,z); \
	c->R[OFF((z+1),KEYP)] ^= t3; \
	t3 ^= c->sbuf; \
	WORD2BYTE(t3,buf+(z*4)); \
    }
void
shn_encrypt(shn_ctx *c, uint8_t *buf, int nbytes)
{
    WORD	t3 = 0;

    /* handle any previously buffered bytes */
    if (c->nbuf != 0) {
	while (c->nbuf != 0 && nbytes != 0) {
	    c->mbuf ^= *buf << (32 - c->nbuf);
	    *buf ^= (c->sbuf >> (32 - c->nbuf)) & 0xFF;
	    ++buf;
	    c->nbuf -= 8;
	    --nbytes;
	}
	if (c->nbuf != 0) /* not a whole word yet */
	    return;
	/* LFSR already cycled */
	macfunc(c, c->mbuf);
    }

    /* do lots at a time, if there's enough to do */
    while (4*N <= nbytes)
    {
	EROUND( 0);
	EROUND( 1);
	EROUND( 2);
	EROUND( 3);
	EROUND( 4);
	EROUND( 5);
	EROUND( 6);
	EROUND( 7);
	EROUND( 8);
	EROUND( 9);
	EROUND(10);
	EROUND(11);
	EROUND(12);
	EROUND(13);
	EROUND(14);
	EROUND(15);
	buf += 4*N;
	nbytes -= 4*N;
    }

    /* do small or odd size buffers the slow way */
    while (4 <= nbytes) {
	cycle(c);
	t3 = BYTE2WORD(buf);
	macfunc(c, t3);
	t3 ^= c->sbuf;
	WORD2BYTE(t3, buf);
	nbytes -= 4;
	buf += 4;
    }

    /* handle any trailing bytes */
    if (nbytes != 0) {
	cycle(c);
	c->mbuf = 0;
	c->nbuf = 32;
	while (c->nbuf != 0 && nbytes != 0) {
	    c->mbuf ^= *buf << (32 - c->nbuf);
	    *buf ^= (c->sbuf >> (32 - c->nbuf)) & 0xFF;
	    ++buf;
	    c->nbuf -= 8;
	    --nbytes;
	}
    }
}

/* Combined MAC and decryption.
 * Note that plaintext is accumulated for MAC.
 */
#undef DROUND
#define DROUND(z) \
    { register WORD t, t3; \
	STEP(c,z); \
	t3 = BYTE2WORD(buf+(z*4)); \
	t3 ^= c->sbuf; \
	CRCFUNC(c,t3,z); \
	c->R[OFF((z+1),KEYP)] ^= t3; \
	WORD2BYTE(t3, buf+(z*4)); \
    }
void
shn_decrypt(shn_ctx *c, uint8_t *buf, int nbytes)
{
    WORD	t3 = 0;

    /* handle any previously buffered bytes */
    if (c->nbuf != 0) {
	while (c->nbuf != 0 && nbytes != 0) {
	    *buf ^= (c->sbuf >> (32 - c->nbuf)) & 0xFF;
	    c->mbuf ^= *buf << (32 - c->nbuf);
	    ++buf;
	    c->nbuf -= 8;
	    --nbytes;
	}
	if (c->nbuf != 0) /* not a whole word yet */
	    return;
	/* LFSR already cycled */
	macfunc(c, c->mbuf);
    }

    /* now do lots at a time, if there's enough */
    while (4*N <= nbytes)
    {
	DROUND( 0);
	DROUND( 1);
	DROUND( 2);
	DROUND( 3);
	DROUND( 4);
	DROUND( 5);
	DROUND( 6);
	DROUND( 7);
	DROUND( 8);
	DROUND( 9);
	DROUND(10);
	DROUND(11);
	DROUND(12);
	DROUND(13);
	DROUND(14);
	DROUND(15);
	buf += 4*N;
	nbytes -= 4*N;
    }

    /* do small or odd size buffers the slow way */
    while (4 <= nbytes) {
	cycle(c);
	t3 = BYTE2WORD(buf);
	t3 ^= c->sbuf;
	macfunc(c, t3);
	WORD2BYTE(t3, buf);
	nbytes -= 4;
	buf += 4;
    }

    /* handle any trailing bytes */
    if (nbytes != 0) {
	cycle(c);
	c->mbuf = 0;
	c->nbuf = 32;
	while (c->nbuf != 0 && nbytes != 0) {
	    *buf ^= (c->sbuf >> (32 - c->nbuf)) & 0xFF;
	    c->mbuf ^= *buf << (32 - c->nbuf);
	    ++buf;
	    c->nbuf -= 8;
	    --nbytes;
	}
    }
}

/* Having accumulated a MAC, finish processing and return it.
 * Note that any unprocessed bytes are treated as if
 * they were encrypted zero bytes, so plaintext (zero) is accumulated.
 */
void
shn_finish(shn_ctx *c, uint8_t *buf, int nbytes)
{
    int		i;

    /* handle any previously buffered bytes */
    if (c->nbuf != 0) {
	/* LFSR already cycled */
	macfunc(c, c->mbuf);
    }

    /* perturb the MAC to mark end of input.
     * Note that only the stream register is updated, not the CRC. This is an
     * action that can't be duplicated by passing in plaintext, hence
     * defeating any kind of extension attack.
     */
    cycle(c);
    ADDKEY(INITKONST ^ (c->nbuf << 3));
    c->nbuf = 0;

    /* now add the CRC to the stream register and diffuse it */
    for (i = 0; i < N; ++i)
        c->R[i] ^= c->CRC[i];
    shn_diffuse(c);

    /* produce output from the stream buffer */
    while (nbytes > 0) {
        cycle(c);
	if (nbytes >= 4) {
	    WORD2BYTE(c->sbuf, buf);
	    nbytes -= 4;
	    buf += 4;
	}
	else {
	    for (i = 0; i < nbytes; ++i)
		buf[i] = Byte(c->sbuf, i);
	    break;
	}
    }
}
