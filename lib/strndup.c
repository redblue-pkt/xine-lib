#include "config.h"

#ifndef HOST_OS_DARWIN
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include <string.h>

char *_xine_private_strndup(const char *s, size_t n) {
  char *ret;
  
  ret = malloc (n + 1);
  strncpy(ret, s, n);
  ret[n] = '\0';
  return ret;
}
