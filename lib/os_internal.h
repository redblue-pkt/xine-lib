#ifndef _XINE_OS_INTERNAL_H
#define _XINE_OS_INTERNAL_H

#include <stddef.h>
#include "os_types.h"


#if defined (__SVR4) && defined (__sun)
#  include <sys/int_types.h>

/* maybe needed for FreeBSD 4-STABLE */
/* 
#elif defined (__FreeBSD__)
#  include <stdint.h>
*/

#endif


#if defined(WIN32)
#  define XINE_PATH_SEPARATOR_STRING ";"
#  define XINE_PATH_SEPARATOR_CHAR ';'
#  define XINE_DIRECTORY_SEPARATOR_STRING "\\"
#  define XINE_DIRECTORY_SEPARATOR_CHAR '\\'
#else
#  define XINE_PATH_SEPARATOR_STRING ":"
#  define XINE_PATH_SEPARATOR_CHAR ':'
#  define XINE_DIRECTORY_SEPARATOR_STRING "/"
#  define XINE_DIRECTORY_SEPARATOR_CHAR '/'
#endif


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
struct timezone;
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

/* replacement of timegm */
#ifndef HAVE_TIMEGM
#include <time.h>
#define timegm(TM) _xine_private_timegm((TM))
time_t _xine_private_timegm(struct tm *tm);
#endif

/* replacement of unsetenv */
#ifndef HAVE_UNSETENV
#define unsetenv(NAME) _xine_private_unsetenv((NAME))
void _xine_private_unsetenv(const char *name);
#endif

/* macross needed for MSVC */
#ifdef _MSC_VER
#  define snprintf _snprintf
#  define vsnprintf _vsnprintf
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
#  define M_PI 3.14159265358979323846
#endif

#ifdef WIN32
#  include <io.h>
#  ifdef _MSC_VER
#    include <direct.h>
#  else
#    define mkdir(A, B) _mkdir((A))
#  endif

#  ifndef S_ISDIR
#    define S_ISDIR(m) ((m) & _S_IFDIR)
#  endif

#  ifndef S_ISREG
#    define S_ISREG(m) ((m) & _S_IFREG)
#  endif

#  ifndef S_ISBLK
#    define S_ISBLK(m) 0
#  endif

#  ifndef S_ISCHR
#    define S_ISCHR(m) 0
#  endif

#  ifndef S_ISLNK
#    define S_ISLNK(mode)  0
#  endif

#  ifndef S_ISSOCK
#    define S_ISSOCK(mode) 0
#  endif

#  ifndef S_ISFIFO
#    define S_ISFIFO(mode) 0
#  endif

#  ifndef S_IXUSR
#    define S_IXUSR S_IEXEC
#  endif

#  ifndef S_IXGRP
#    define S_IXGRP S_IEXEC
#  endif

#  ifndef S_IXOTH
#    define S_IXOTH S_IEXEC
#  endif

#  if !S_IXUGO
#    define S_IXUGO (S_IXUSR | S_IXGRP | S_IXOTH)
#  endif

#endif

#ifndef HAVE_READLINK
#  define readlink(PATH, BUF, BUFSIZE) 0
#endif

/* replacing lstat by stat */
#ifndef HAVE_LSTAT
#  define lstat(FILENAME, BUF) stat((FILENAME), (BUF))
#endif

/* replacements of dirent for MSVC platform */
#ifndef HAVE_OPENDIR
typedef struct DIR DIR;

struct dirent {
  unsigned short d_reclen;
  char *d_name;
};

DIR           *_xine_private_opendir(const char *);
int           _xine_private_closedir(DIR *);
struct dirent *_xine_private_readdir(DIR *);
void          _xine_private_rewinddir(DIR *);

#define opendir(DIRENT_NAME) _xine_private_opendir((DIRENT_NAME))
#define closedir(DIRENT_DIR) _xine_private_closedir((DIRENT_DIR))
#define readdir(DIRENT_DIR) _xine_private_readdir((DIRENT_DIR))
#define rewinddir(DIRENT_DIR) _xine_private_rewinddir((DIRENT_DIR))

#endif

#endif
