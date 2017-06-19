#ifndef __VERIFICATION_H__
#define __VERIFICATION_H__

#include <stdint.h>

struct verification_setup_context;
struct verification_verify_context;

/* When you have the pin-code (must be 4 bytes), create a new context with this
 * function and then call verification_setup_request1()
 */
struct verification_setup_context *
verification_setup_new(const char *pin);
void
verification_setup_free(struct verification_setup_context *sctx);

/* Returns last error message
 */
const char *
verification_setup_errmsg(struct verification_setup_context *sctx);

uint8_t *
verification_setup_request1(uint32_t *len, struct verification_setup_context *sctx);
uint8_t *
verification_setup_request2(uint32_t *len, struct verification_setup_context *sctx);
uint8_t *
verification_setup_request3(uint32_t *len, struct verification_setup_context *sctx);

int
verification_setup_response1(struct verification_setup_context *sctx, const uint8_t *data, uint32_t data_len);
int
verification_setup_response2(struct verification_setup_context *sctx, const uint8_t *data, uint32_t data_len);
int
verification_setup_response3(struct verification_setup_context *sctx, const uint8_t *data, uint32_t data_len);

/* Returns a 0-terminated string that is the authorisation key. The caller
 * should save it and use it later to initialize verification_verify_new().
 * Note that the pointer becomes invalid when you free sctx.
 */
int
verification_setup_result(const char **authorisation_key, struct verification_setup_context *sctx);


/* When you have completed the setup you can extract a key with
 * verification_setup_result(). Give the string as input to this function to
 * create a verification context and then call verification_verify_request1()
 */
struct verification_verify_context *
verification_verify_new(const char *authorisation_key);
void
verification_verify_free(struct verification_verify_context *vctx);

/* Returns last error message
 */
const char *
verification_verify_errmsg(struct verification_verify_context *vctx);

uint8_t *
verification_verify_request1(uint32_t *len, struct verification_verify_context *vctx);
uint8_t *
verification_verify_request2(uint32_t *len, struct verification_verify_context *vctx);

int
verification_verify_response1(struct verification_verify_context *vctx, const uint8_t *data, uint32_t data_len);

#endif  /* !__VERIFICATION_H__ */
