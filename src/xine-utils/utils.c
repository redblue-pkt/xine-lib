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
 * $Id: utils.c,v 1.11 2003/01/12 16:38:08 holstsn Exp $
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
#include <pthread.h>

#include "compat.h"

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
  
  while ((int) ptr % alignment)
    ptr++;
    
  return ptr;
}

#ifndef BUFSIZ
#define BUFSIZ 256
#endif

const char *xine_get_homedir(void) {
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
    strncpy(homedir, pw->pw_dir, sizeof(homedir));
    homedir[sizeof(homedir) - 1] = '\0';
  }

  if(!homedir[0]) {
    printf("xine_get_homedir: Unable to get home directory, set it to /tmp.\n");
    strcpy(homedir, "/tmp");
  }

  return homedir;
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
