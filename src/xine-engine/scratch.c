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
 * $Id: scratch.c,v 1.8 2002/09/06 18:13:12 mroi Exp $
 *
 * top-level xine functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>  /* For memset */

#include "scratch.h"
#include "xineutils.h"

/*
#define LOG
*/

static void scratch_printf (scratch_buffer_t *this, const char *format, va_list argp) {

  vsnprintf (this->lines[this->cur], 1023, format, argp);

#ifdef LOG
  printf ("scratch: printing format %s to line %d\n",
	  format, this->cur);
#endif

  this->cur = (this->cur + 1) % this->num_lines;
}

static const char **scratch_get_content (scratch_buffer_t *this) {

  int i, j;

  for(i = 0, j = (this->cur - 1); i < this->num_lines; i++, j--) {

    if(j < 0)
      j = (this->num_lines - 1);

    this->ordered[i] = this->lines[j];

#ifdef LOG
    printf ("scratch: line %d contains >%s<\n", i , this->lines[j]);
#endif

  }

  return this->ordered;

}

static void scratch_dispose (scratch_buffer_t *this) {

  int i;

  for(i = 0; i < this->num_lines; i++ ) {
    free (this->lines[i]);
  }

  free (this->lines);
  free (this->ordered);
  free (this);

}

scratch_buffer_t *new_scratch_buffer (int num_lines) {

  scratch_buffer_t *this;
  int i;

  this = xine_xmalloc (sizeof (scratch_buffer_t));

  this->lines   = xine_xmalloc (sizeof (char *) * (num_lines + 1));
  this->ordered = xine_xmalloc (sizeof (char *) * (num_lines + 1));
  for (i=0; i<num_lines; i++) {
    this->lines[i]   = (char *) xine_xmalloc (sizeof(char) * 1024);
    memset(this->lines[i], 0, sizeof(this->lines[i]));
  }

  this->ordered[i]  = NULL;
  this->lines[i]    = NULL;

  this->scratch_printf = scratch_printf;
  this->get_content = scratch_get_content;
  this->dispose = scratch_dispose;

  this->num_lines   = num_lines;
  this->cur         = 0;

  return this;
}
