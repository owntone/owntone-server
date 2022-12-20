
#ifndef __HTTPD_INTERNAL_H__
#define __HTTPD_INTERNAL_H__

#include "httpd.h" // TODO remove and transfer

// Must be in sync with modules[] in httpd.c
enum httpd_modules
{
  MODULE_DACP,
  MODULE_DAAP,
  MODULE_JSONAPI,
  MODULE_ARTWORKAPI,
  MODULE_STREAMING,
  MODULE_OAUTH,
  MODULE_RSP,
};

struct httpd_module
{
  const char *name;
  enum httpd_modules type;
  char initialized;

  // Null-terminated list of URL subpath that the module accepts e.g., /subpath/morepath/file.mp3
  const char *subpaths[16];
  // Null-terminated list of URL fullparhs that the module accepts e.g., /fullpath
  const char *fullpaths[16];
  // Pointer to the module's handler definitions
  struct httpd_uri_map *handlers;

  int (*init)(void);
  void (*deinit)(void);
  void (*request)(struct httpd_request *hreq);
};

#endif /* !__HTTPD_INTERNAL_H__ */
