#include "config.h"

#include <stdlib.h>

void _xine_private_unsetenv(const char *name) {
  putenv(name);
}
