/*
 * Copyright (C) 2000-2003 the xine project
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
 * stream metainfo helper functions
 * hide some xine engine details from demuxers and reduce code duplication
 *
 * $id$ 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "info_helper.h"

/* Remove trailing separator chars (\n,\r,\t, space,...)
 * at the end of the string
 */
static void chomp (char *str) {
  int i, len;

  len = strlen(str);
  i = len - 1;
  
  while (((unsigned char)str[i] <= 32) && (i >= 0)) {
    str[i] = 0;
    i--;
  }
}

void xine_set_stream_info(xine_stream_t *stream, int info, int value) {
  stream->stream_info [info] = value;
}

void xine_set_meta_info(xine_stream_t *stream, int info, char *str) {
  if (stream->meta_info [info])
    free(stream->meta_info [info]);
  stream->meta_info [info] = strdup(str);
  chomp(stream->meta_info [info]);
}

void xine_set_meta_info2(xine_stream_t *stream, int info, char *buf, int len) {
  char *tmp;

  if (stream->meta_info [info])
    free(stream->meta_info [info]);

  tmp = malloc(len + 1);
  xine_fast_memcpy(tmp, buf, len);
  tmp[len] = '\0';

  stream->meta_info [info] = tmp;
  chomp(stream->meta_info [info]);
}
