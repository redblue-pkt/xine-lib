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
 * $Id: scratch.c,v 1.1 2001/12/09 17:24:39 guenter Exp $
 *
 * top-level xine functions
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>

#include "scratch.h"
#include "xineutils.h"

static void scratch_printf (scratch_buffer_t *this, const char *format, va_list argp) {

  vsprintf (this->lines[this->cur], format, argp);

  this->cur = (this->cur + 1) % this->num_lines;

}

static char **scratch_get_content (scratch_buffer_t *this) {

  int i;

  for (i=0; i<this->num_lines; i++) {

    this->lines[i]   = xine_xmalloc (1024);
    this->ordered[i] = this->lines[(this->cur + i) % this->num_lines];

  }

  return this->ordered;

}

scratch_buffer_t *new_scratch_buffer (int num_lines) {

  scratch_buffer_t *this;
  int i;

  this = xine_xmalloc (sizeof (scratch_buffer_t));

  this->lines   = xine_xmalloc (sizeof (char *) * num_lines+1);
  this->ordered = xine_xmalloc (sizeof (char *) * num_lines+1);
  for (i=0; i<num_lines; i++) {
    this->lines[i]   = xine_xmalloc (1024);
  }

  this->ordered[i]  = NULL;
  this->lines[i]    = NULL;

  this->printf      = scratch_printf;
  this->get_content = scratch_get_content;

  this->num_lines   = num_lines;
  this->cur         = 0;

  return this;
}
