/*
 * Copyright (C) 2000-2002 the xine project
 * 
 * This file is part of xine, a free video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: utils.c,v 1.21 2003/12/06 16:10:01 miguelfreitas Exp $
 *
 */
#define	_POSIX_PTHREAD_SEMANTICS 1	/* for 5-arg getpwuid_r on solaris */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xineutils.h"

#include <errno.h>
#include <pwd.h>
#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif
#if HAVE_UCONTEXT_H
#include <ucontext.h>
#endif


void *xine_xmalloc(size_t size) {
  void *ptr;

  /* prevent xine_xmalloc(0) of possibly returning NULL */
  if( !size )
    size++;

  if((ptr = calloc(1, size)) == NULL) {
    fprintf(stderr, "%s: malloc() failed: %s.\n",
	    __XINE_FUNCTION__, strerror(errno));
    return NULL;
  }

  return ptr;
}

void *xine_xmalloc_aligned(size_t alignment, size_t size, void **base) {

  char *ptr;
  
  *base = ptr = xine_xmalloc (size+alignment);
  
  while ((size_t) ptr % alignment)
    ptr++;
    
  return ptr;
}

#ifndef BUFSIZ
#define BUFSIZ 256
#endif

const char *xine_get_homedir(void) {

#ifdef WIN32
	return XINE_HOMEDIR;
#else

  struct passwd pwd, *pw = NULL;
  static char homedir[BUFSIZ] = {0,};

  if(homedir[0])
    return homedir;

#ifdef HAVE_GETPWUID_R
  if(getpwuid_r(getuid(), &pwd, homedir, sizeof(homedir), &pw) != 0 || pw == NULL) {
#else
  if((pw = getpwuid(getuid())) == NULL) {
#endif
    char *tmp = getenv("HOME");
    if(tmp) {
      strncpy(homedir, tmp, sizeof(homedir));
      homedir[sizeof(homedir) - 1] = '\0';
    }
  } else {
    char *s = strdup(pw->pw_dir);
    strncpy(homedir, s, sizeof(homedir));
    homedir[sizeof(homedir) - 1] = '\0';
    free(s);
  }

  if(!homedir[0]) {
    printf("xine_get_homedir: Unable to get home directory, set it to /tmp.\n");
    strcpy(homedir, "/tmp");
  }

  return homedir;
#endif /* _MSC_VER */
}

char *xine_chomp(char *str) {
  char *pbuf;

  pbuf = str;

  while(*pbuf != '\0') pbuf++;

  while(pbuf > str) {
    if(*pbuf == '\r' || *pbuf == '\n' || *pbuf == '"') pbuf = '\0';
    pbuf--;
  }

  while(*pbuf == '=' || *pbuf == '"') pbuf++;

  return pbuf;
}


/*
 * a thread-safe usecond sleep
 */
void xine_usec_sleep(unsigned usec) {
#if HAVE_NANOSLEEP
  /* nanosleep is prefered on solaris, because it's mt-safe */
  struct timespec ts;

  ts.tv_sec =   usec / 1000000;
  ts.tv_nsec = (usec % 1000000) * 1000;
  nanosleep(&ts, NULL);
#else
  usleep(usec);
#endif
}


/* Obtain a backtrace and print it to stdout. */
void xine_print_trace (void) {
#if HAVE_BACKTRACE
  /* Code Taken from GNU C Library manual */
  void *array[10];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace (array, 10);
  strings = backtrace_symbols (array, size);

  printf ("Obtained %lu stack frames.\n", (unsigned long) size);

  for (i = 0; i < size; i++) {
     printf ("%s\n", strings[i]);
  }
  free (strings);
#elif HAVE_PRINTSTACK
  printstack(STDOUT_FILENO);
#else
  printf("stack backtrace not available.\n");
#endif
}

/* print a hexdump of length bytes from the data given in buf */
void xine_hexdump (const char *buf, int length) {
  int i,j;
  unsigned char c;

  /* printf ("Hexdump: %i Bytes\n", length);*/
  for(j=0; j<69; j++)
    printf ("-");
  printf ("\n");

  j=0;
  while(j<length) {
    printf ("%04X ",j);
    for (i=j; i<j+16; i++) {
      if( i<length )
        printf ("%02X ", (unsigned char) buf[i]);
      else
        printf("   ");
    }
    for (i=j;i<(j+16<length?j+16:length);i++) {
      c=buf[i];
      if ((c>=32) && (c<127))
        printf ("%c", c);
      else
        printf (".");
    }
    j=i;
    printf("\n");
  }

  for(j=0; j<69; j++)
    printf("-");
  printf("\n");
}


#ifndef HAVE_BASENAME

#define FILESYSTEM_PREFIX_LEN(filename) 0
#define ISSLASH(C) ((C) == '/')

/*
 * get base name
 *
 * (adopted from sh-utils)
 */
char *basename (char const *name) {
  char const *base = name + FILESYSTEM_PREFIX_LEN (name);
  char const *p;

  for (p = base; *p; p++)
    {
      if (ISSLASH (*p))
	{
	  /* Treat multiple adjacent slashes like a single slash.  */
	  do p++;
	  while (ISSLASH (*p));

	  /* If the file name ends in slash, use the trailing slash as
	     the basename if no non-slashes have been found.  */
	  if (! *p)
	    {
	      if (ISSLASH (*base))
		base = p - 1;
	      break;
	    }

	  /* *P is a non-slash preceded by a slash.  */
	  base = p;
	}
    }

  return (char *) base;
}
#endif
