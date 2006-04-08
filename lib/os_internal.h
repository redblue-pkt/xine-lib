#ifndef _XINE_OS_INTERNAL_H
#define _XINE_OS_INTERNAL_H

#include <stddef.h>

#ifdef HOST_OS_DARWIN
  /* Darwin (Mac OS X) needs __STDC_LIBRARY_SUPPORTED__ for SCNx64 and
   * SCNxMAX macros */
#  ifndef __STDC_LIBRARY_SUPPORTED__
#    define __STDC_LIBRARY_SUPPORTED__
#  endif /* __STDC_LIBRARY_SUPPORTED__ */
#endif

#if defined (__SVR4) && defined (__sun)
#  include <sys/int_types.h>
#endif

#include <inttypes.h>


#if defined(WIN32) || defined(__CYGWIN__)
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

/* handle non-standard function names */
#if !defined(HAVE_SNPRINTF) && defined(HAVE__SNPRINTF)
#  define snprintf _snprintf
#endif
#if !defined(HAVE_VSNPRINTF) && defined(HAVE__VSNPRINTF)
#  define vsnprintf _vsnprintf
#endif
#if !defined(HAVE_STRCASECMP) && defined(HAVE__STRICMP)
#  define strcasecmp _stricmp
#endif
#if !defined(HAVE_STRNCASECMP) && defined(HAVE__STRNICMP)
#  define strncasecmp _strnicmp
#endif

#include <math.h>
#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#ifndef HAVE_LRINTF
#define lrint(X) (long)((X) + ((X) >= 0 ? 0.5 : -0.5))
#endif
#ifndef HAVE_RINTF
#define rint(X) (int)((X) + ((X) >= 0 ? 0.5 : -0.5))
#endif

#ifdef WIN32
#  include <io.h>
#  ifdef _MSC_VER
#    include <direct.h>
#  endif
#  ifdef HAVE_SYS_STAT_H
#    include <sys/stat.h>
#  endif
#  define mkdir(A, B) _mkdir((A))

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

/*
 * workaround compatibility code due to 'near' and 'far' keywords in windef.h
 * (do it only inside ffmpeg)
 */
#  ifdef HAVE_AV_CONFIG_H
#    include <windef.h>
#    ifdef near
#      undef near
#    endif
#    ifdef far
#      undef far
#    endif
     /* it sucks everywhere :-) */
#    define near win32_sucks_near
#    define far win32_sucks_far
#  endif /* av_config */

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
