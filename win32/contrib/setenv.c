
#include <xinesuppt.h>

#include <stdlib.h>
#include <string.h>

/* This function will leak a small amount of memory */
void setenv(const char *name, const char *val, int _xx)
{
  int len  = strlen(name) + strlen(val) + 2;
  char *env = malloc(len);

  if (env != NULL) {
    strcpy(env, name);
    strcat(env, "=");
    strcat(env, val);
    putenv(env);
  }
}
