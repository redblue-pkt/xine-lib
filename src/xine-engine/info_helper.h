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

#ifndef INFO_HELPER_H
#define INFO_HELPER_H

#include "xine_internal.h"

/*
 * set a stream info
 *
 * params :
 *  *stream        the xine stream
 *   info          stream info id (see xine.h, XINE_STREAM_INFO_*)
 *   value         the value to assign
 *
 */
void xine_set_stream_info(xine_stream_t *stream, int info, int value);

/*
 * set a stream meta info
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *  *str           null-terminated string
 *
 */
void xine_set_meta_info(xine_stream_t *stream, int info, char *str);

/*
 * set a stream meta info
 *
 * params :
 *  *stream        the xine stream
 *   info          meta info id (see xine.h, XINE_META_INFO_*)
 *  *buf           char buffer (not a null-terminated string)
 *   len           length of the metainfo
 *
 */
void xine_set_meta_info2(xine_stream_t *stream, int info, char *buf, int len);

#endif /* INFO_HELPER_H */
