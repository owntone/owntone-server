/* $Id: $ */
/* Shannon: Shannon stream cipher and MAC header files */

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

#ifndef _SHN_DEFINED
#define _SHN_DEFINED 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SHANNON_N 16

typedef struct {
    uint32_t R[SHANNON_N]; /* Working storage for the shift register */
    uint32_t CRC[SHANNON_N]; /* Working storage for CRC accumulation */
    uint32_t initR[SHANNON_N]; /* saved register contents */
    uint32_t konst; /* key dependent semi-constant */
    uint32_t sbuf; /* encryption buffer */
    uint32_t mbuf; /* partial word MAC buffer */
    int nbuf; /* number of part-word stream bits buffered */
} shn_ctx;

/* interface definitions */
void shn_key(shn_ctx *c, const uint8_t key[], int keylen); /* set key */
void shn_nonce(shn_ctx *c, const uint8_t nonce[], int nlen); /* set Init Vector */
void shn_stream(shn_ctx *c, uint8_t *buf, int nbytes); /* stream cipher */
void shn_maconly(shn_ctx *c, uint8_t *buf, int nbytes); /* accumulate MAC */
void shn_encrypt(shn_ctx *c, uint8_t *buf, int nbytes); /* encrypt + MAC */
void shn_decrypt(shn_ctx *c, uint8_t *buf, int nbytes); /* decrypt + MAC */
void shn_finish(shn_ctx *c, uint8_t *buf, int nbytes); /* finalise MAC */

#ifdef __cplusplus
}
#endif

#endif /* _SHN_DEFINED */
