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
 * $Id: input_stdin_fifo.c,v 1.1 2001/04/18 22:34:05 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "input_plugin.h"

static uint32_t xine_debug;

static int input_file_handle;

/* ------------------------------------------------------------------------- */
/*
 *
 */
static void input_plugin_init(void) {

  input_file_handle = -1;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static int input_plugin_open(const char *mrl) {
  char *filename;
  char *pfn;

  if(!strncasecmp(mrl, "stdin:", 6) 
      || !strncmp(mrl, "-", 1)) {
    filename = "/dev/stdin";
  } 
  else if(!strncasecmp(mrl, "fifo:", 5)) {

    if((pfn = strrchr((mrl+5), ':')) != NULL) {
      filename = ++pfn;
    }
    else {
      filename = (char *) &mrl[5];
    }

  } 
  else {
    filename = (char *) mrl;
  }
  
#ifdef DEBUG
  fprintf(stderr, "%s(%d): opening >%s< file\n", 
	  __FILE__, __LINE__, filename);
#endif

  input_file_handle = open(filename, O_RDONLY);

  if(input_file_handle == -1) {
    return 0;
  }

  return 1;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static uint32_t input_plugin_read(char *buf, uint32_t nlen) {

  int n, nBytesRead;

  nBytesRead = 0;

  while (nBytesRead < nlen) {
    n = read(input_file_handle, &buf[nBytesRead], nlen-nBytesRead);

    if (n<0)
      return n;
    else if (!n)
      return nBytesRead;

    nBytesRead += n;
  }
  return nBytesRead;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static off_t input_plugin_seek(off_t offset, int origin) {

  return lseek(input_file_handle, offset, origin);
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static off_t input_plugin_get_length(void) {
  struct stat buf ;

  if(fstat(input_file_handle, &buf) == 0) {
    return buf.st_size;
  } 
  else { 
    fprintf(stderr, "%s(%d): fstat() failed: %s\n",
	    __FILE__, __LINE__, strerror(errno));
  }

  return 0;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static uint32_t input_plugin_get_capabilities(void) {

  return 0;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static uint32_t input_plugin_get_blocksize(void) {

  return 0;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static int input_plugin_eject (void) {
  return 1;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static void input_plugin_close(void) {

#ifdef DEBUG
  fprintf(stderr, "%s(%d): closing input\n",
	 __FILE__, __LINE__);
#endif

  close(input_file_handle);
  input_file_handle = -1;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static char *input_plugin_get_identifier(void) {

  return "stdin_fifo";
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static int input_plugin_is_branch_possible (const char *next_mrl) {

  return 0;
}
/* ------------------------------------------------------------------------- */
/*
 *
 */
static input_plugin_t plugin_op = {
  NULL,
  NULL,
  input_plugin_init,
  input_plugin_open,
  input_plugin_read,
  input_plugin_seek,
  input_plugin_get_length,
  input_plugin_get_capabilities,
  NULL,
  input_plugin_get_blocksize,
  input_plugin_eject,
  input_plugin_close,
  input_plugin_get_identifier,
  NULL,
  input_plugin_is_branch_possible,
  NULL
};
/* ------------------------------------------------------------------------- */
/*
 *
 */
input_plugin_t *input_plugin_getinfo(uint32_t dbglvl) {

  xine_debug = dbglvl;

  return &plugin_op;
}
/* ------------------------------------------------------------------------- */
