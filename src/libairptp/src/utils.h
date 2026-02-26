#ifndef __AIRPTP_UTILS_H__
#define __AIRPTP_UTILS_H__

#include <sys/socket.h>
#include <netinet/in.h>

#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H)
# include <sys/endian.h>
#elif defined(HAVE_LIBKERN_OSBYTEORDER_H)
#include <libkern/OSByteOrder.h>
#define htobe16(x) OSSwapHostToBigInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#endif

#define ARRAY_SIZE(x) ((unsigned int)(sizeof(x) / sizeof((x)[0])))

union utils_net_sockaddr
{
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
  struct sockaddr sa;
  struct sockaddr_storage ss;
};

int
utils_net_bind(const char *node, unsigned short port);

int
utils_net_sockaddr_get(union utils_net_sockaddr *naddr, const char *addr, unsigned short port);

int
utils_net_address_get(char *addr, size_t addr_len, union utils_net_sockaddr *naddr);

uint32_t
utils_djb_hash(const void *data, size_t len);

#endif // __AIRPTP_UTILS_H__
