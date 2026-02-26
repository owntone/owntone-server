/*
MIT License

Copyright (c) 2026 OwnTone

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>

#include "utils.h"

extern struct airptp_callbacks __thread airptp_cb;

int
net_bind(const char *node, unsigned short port)
{
  struct addrinfo hints = { 0 };
  struct addrinfo *servinfo;
  struct addrinfo *ptr;
  char strport[8];
  int flags;
  int yes = 1;
  int no = 0;
  int fd = -1;
  int ret;

  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_family = AF_INET6;
  hints.ai_flags = node ? 0 : AI_PASSIVE;

  snprintf(strport, sizeof(strport), "%hu", port);
  ret = getaddrinfo(node, strport, &hints, &servinfo);
  if (ret < 0)
    goto error;

  for (ptr = servinfo; ptr != NULL; ptr = ptr->ai_next)
    {
      if (fd >= 0)
	close(fd);

      fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if (fd < 0)
	continue;

      flags = fcntl(fd, F_GETFL, 0);
      if (flags < 0)
	continue;
      ret = fcntl(fd, F_SETFL, flags | O_CLOEXEC);
      if (ret < 0)
	continue;

      // TODO libevent sets this, we do the same?
      ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
      if (ret < 0)
	continue;

      ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
      if (ret < 0)
	continue;

      // We want to make sure the socket is dual stack
      ret = (ptr->ai_family == AF_INET6) ? setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) : 0;
      if (ret < 0)
	continue;

      ret = bind(fd, ptr->ai_addr, ptr->ai_addrlen);
      if (ret < 0)
	continue;

      break;
    }

  freeaddrinfo(servinfo);

  if (!ptr)
    goto error;

  return fd;

 error:
  if (fd >= 0)
    close(fd);
  return -1;
}

int
net_sockaddr_get(union net_sockaddr *naddr, const char *addr, unsigned short port)
{
  memset(naddr, 0, sizeof(union net_sockaddr));

  if (inet_pton(AF_INET, addr, &naddr->sin.sin_addr) == 1)
    {
      naddr->sin.sin_family = AF_INET;
      naddr->sin.sin_port = htons(port);
      return 0;
    }

  if (inet_pton(AF_INET6, addr, &naddr->sin6.sin6_addr) == 1)
    {
      naddr->sin6.sin6_family = AF_INET6;
      naddr->sin6.sin6_port = htons(port);
      return 0;
    }

  return -1;
}

int
net_address_get(char *addr, size_t addr_len, union net_sockaddr *naddr)
{
  const char *s = NULL;

  memset(addr, 0, addr_len); // Just in case caller doesn't check for errors

  if (naddr->sa.sa_family == AF_INET6)
     s = inet_ntop(AF_INET6, &naddr->sin6.sin6_addr, addr, addr_len);
  else if (naddr->sa.sa_family == AF_INET)
     s = inet_ntop(AF_INET, &naddr->sin.sin_addr, addr, addr_len);

  if (!s)
    return -1;

  return 0;
}

uint32_t
djb_hash(const void *data, size_t len)
{
  const unsigned char *bytes = data;
  uint32_t hash = 5381;

  while (len--)
    {
      hash = ((hash << 5) + hash) + *bytes;
      bytes++;
    }

  return hash;
}
