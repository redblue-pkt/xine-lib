/* 
 * Copyright (C) 2000-2001 the xine project
 * 
 * This file is part of xine, a unix video player.
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
 * $Id: utils.c,v 1.5 2001/10/03 15:16:50 jkeil Exp $
 *
 */
#define	_POSIX_PTHREAD_SEMANTICS 1	/* for 5-arg getpwuid_r on solaris */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <sys/types.h>

#ifndef	__GNUC__
#define __FUNCTION__	__func__
#endif


/*
 *
 */
void *xmalloc(size_t size) {
  void *ptr;

  if((ptr = malloc(size)) == NULL) {
    fprintf(stderr, "%s: malloc() failed: %s.\n",
	    __FUNCTION__, strerror(errno));
    return NULL;
  }

  memset(ptr, 0, size);

  return ptr;
}

/*
 *
 */
void *xmalloc_aligned (size_t alignment, size_t size) {
  char *pMem;

  pMem = xmalloc (size+alignment);

  while ((int) pMem % alignment)
    pMem++;

  return pMem;
}

/*
 *
 */
const char *get_homedir(void) {
  struct passwd *pw = NULL;
  char *homedir = NULL;
#ifdef HAVE_GETPWUID_R
  int ret;
  struct passwd pwd;
  char *buffer = NULL;
  int bufsize = 128;

  buffer = (char *) xmalloc(bufsize);
  
  if((ret = getpwuid_r(getuid(), &pwd, buffer, bufsize, &pw)) < 0) {
#else
  if((pw = getpwuid(getuid())) == NULL) {
#endif
    if((homedir = getenv("HOME")) == NULL) {
      fprintf(stderr, "Unable to get home directory, set it to /tmp.\n");
      homedir = strdup("/tmp");
    }
  }
  else {
    if(pw) 
      homedir = strdup(pw->pw_dir);
  }
  
  
#ifdef HAVE_GETPWUID_R
  if(buffer) 
    free(buffer);
#endif
  
  return homedir;
}

/*
 *
 */
char *chomp(char *str) {
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
 * A thread-safe usecond sleep
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
