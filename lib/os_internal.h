#include <stddef.h>
#include "os_types.h"

/* replacement of strndup */
#ifndef HAVE_STRNDUP
#define strndup(S, N) _xine_private_strndup((S), (N))
char *_xine_private_strndup(const char *s, size_t n);
#endif

/* replacement of basename */
#ifndef HAVE_BASENAME
#define basename(PATH) _xine_private_basename((PATH))
char *_xine_private_basename(char *path);
#endif

/* replacement of hstrerror */
#ifndef HAVE_HSTRERROR
#define hstrerror(ERR) _xine_private_hstrerror((ERR))
const char *_xine_private_hstrerror(int err);
#endif

/* replacement of setenv */
#ifndef HAVE_SETENV
#define setenv(NAME, VALUE, OVERWRITE) _xine_private_setenv((NAME), (VALUE))
int _xine_private_setenv(const char *name, const char *value);
#endif

/* replacement of strtok_r */
#ifndef HAVE_STRTOK_R
#define strtok_r(S, DELIM, PTRPTR) _xine_private_strtok_r((S), (DELIM), (PTRPTR))
char *_xine_private_strtok_r(char *s, const char *delim, char **ptrptr);
#endif

/* replacement of gettimeofday */
#ifndef HAVE_GETTIMEOFDAY
#  ifdef WIN32
#    include <winsock.h>
#  else
#    include <sys/time.h>
#  endif
#  define gettimeofday(TV, TZ) _xine_private_gettimeofday((TV))
int _xine_private_gettimeofday(struct timeval *tv);
#endif

/* replacement of strpbrk */
#ifndef HAVE_STRPBRK
#define strpbrk(S, ACCEPT) _xine_private_strpbrk((S), (ACCEPT))
char *_xine_private_strpbrk(const char *s, const char *accept);
#endif

/* replacement of strsep */
#ifndef HAVE_STRSEP
#define strsep(STRINGP, DELIM) _xine_private_strsep((STRINGP), (DELIM))
char *_xine_private_strsep(char **stringp, const char *delim);
#endif

/* replacing lstat by stat */
#ifndef HAVE_LSTAT
#  define lstat(FILENAME, BUF) stat((FILENAME), (BUF))
#endif

/* macross needed for MSVC */
#ifdef _MSC_VER
#  define snprintf _snprintf
#  define vsnprintf _vsnprintf
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
#endif
