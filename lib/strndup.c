#include "config.h"

#include <malloc.h>
#include <string.h>

char *_xine_private_strndup(const char *s, size_t n) {
  char *ret;
  
  ret = malloc (n + 1);
  strncpy(ret, s, n);
  ret[n] = '\0';
  return ret;
}
