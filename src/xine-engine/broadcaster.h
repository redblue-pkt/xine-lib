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
 * $Id: broadcaster.h,v 1.1 2003/05/15 20:23:18 miguelfreitas Exp $
 * 
 * broadcaster.h
 *
 */

#ifndef HAVE_BROADCASTER_H
#define HAVE_BROADCASTER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef struct broadcaster_s broadcaster_t;

broadcaster_t *init_broadcaster(xine_stream_t *stream, int port);
void close_broadcaster(broadcaster_t *this);
int get_broadcaster_port(broadcaster_t *this);


#ifdef __cplusplus
}
#endif

#endif

