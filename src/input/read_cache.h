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
 * $Id: read_cache.h,v 1.2 2001/09/11 09:58:15 jkeil Exp $
 */

#ifndef HAVE_READ_CACHE_H
#define HAVE_READ_CACHE_H

#include <inttypes.h>
#include <sys/types.h>
#include "buffer.h"

typedef struct read_cache_s read_cache_t;

read_cache_t *read_cache_new ();

void read_cache_set_fd (read_cache_t *this, int fd);

buf_element_t *read_cache_read_block (read_cache_t *this,
				      off_t pos);

void read_cache_free (read_cache_t *this);

#endif
